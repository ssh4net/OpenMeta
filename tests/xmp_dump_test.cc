// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static uint32_t count_substring(std::string_view s,
                                    std::string_view needle) noexcept
    {
        if (needle.empty()) {
            return 0U;
        }
        uint32_t count = 0U;
        size_t pos     = 0U;
        while (pos < s.size()) {
            const size_t found = s.find(needle, pos);
            if (found == std::string_view::npos) {
                break;
            }
            count += 1U;
            pos = found + needle.size();
        }
        return count;
    }

    static std::vector<std::byte>
    make_utf16le_ascii_bytes(std::string_view s, bool nul_terminate = true)
    {
        std::vector<std::byte> out;
        out.reserve((s.size() + (nul_terminate ? 1U : 0U)) * 2U);
        for (size_t i = 0U; i < s.size(); ++i) {
            out.push_back(static_cast<std::byte>(s[i]));
            out.push_back(std::byte { 0x00 });
        }
        if (nul_terminate) {
            out.push_back(std::byte { 0x00 });
            out.push_back(std::byte { 0x00 });
        }
        return out;
    }

}  // namespace

TEST(XmpDump, EmitsValidPacketAndKey)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry e;
    e.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    e.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    e.origin.block = block;
    e.origin.order_in_block = 0;
    e.origin.wire_type      = WireType { WireFamily::Tiff, 2 /*ASCII*/ };
    e.origin.wire_count     = 5;
    (void)store.add_entry(e);

    store.finalize();

    XmpDumpOptions opts;
    std::vector<std::byte> out(64);

    XmpDumpResult r1
        = dump_xmp_lossless(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r1.status, XmpDumpStatus::OutputTruncated);
    ASSERT_GT(r1.needed, out.size());

    out.resize(static_cast<size_t>(r1.needed));
    const XmpDumpResult r2
        = dump_xmp_lossless(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r2.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r2.entries, 1U);
    ASSERT_EQ(r2.written, r2.needed);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r2.written));
    EXPECT_NE(s.find("<x:xmpmeta"), std::string_view::npos);
    EXPECT_NE(s.find("urn:openmeta:dump:1.0"), std::string_view::npos);
    EXPECT_NE(s.find("exif:ifd0:0x010F"), std::string_view::npos);
    EXPECT_NE(s.find("Q2Fub24="), std::string_view::npos);  // base64("Canon")
}


TEST(XmpDump, SidecarApiLosslessAutoResizesOutput)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry e;
    e.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    e.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    e.origin.block = block;
    e.origin.order_in_block = 0;
    (void)store.add_entry(e);
    store.finalize();

    XmpSidecarOptions opts;
    opts.format               = XmpSidecarFormat::Lossless;
    opts.initial_output_bytes = 16U;

    std::vector<std::byte> out;
    const XmpDumpResult r = dump_xmp_sidecar(store, &out, opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(out.size(), static_cast<size_t>(r.written));
    ASSERT_GE(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             out.size());
    EXPECT_NE(s.find("<x:xmpmeta"), std::string_view::npos);
    EXPECT_NE(s.find("exif:ifd0:0x010F"), std::string_view::npos);
}


TEST(XmpDump, SidecarApiPortableUsesFormatSwitch)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry make;
    make.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    make.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    make.origin.block = block;
    make.origin.order_in_block = 0;
    (void)store.add_entry(make);

    Entry exp;
    exp.key          = make_exif_tag_key(store.arena(), "exififd", 0x829A);
    exp.value        = make_urational(1, 1250);
    exp.origin.block = block;
    exp.origin.order_in_block = 1;
    (void)store.add_entry(exp);

    store.finalize();

    XmpSidecarOptions opts;
    opts.format                = XmpSidecarFormat::Portable;
    opts.initial_output_bytes  = 32U;
    opts.portable.include_exif = true;

    std::vector<std::byte> out;
    const XmpDumpResult r = dump_xmp_sidecar(store, &out, opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(out.size(), static_cast<size_t>(r.written));
    ASSERT_GE(r.entries, 2U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             out.size());
    EXPECT_NE(s.find("<tiff:Make>Canon</tiff:Make>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExposureTime>1/1250</exif:ExposureTime>"),
              std::string_view::npos);
}


TEST(XmpDump, SidecarRequestApiPortableIsDeterministic)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry make;
    make.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    make.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    make.origin.block = block;
    make.origin.order_in_block = 0;
    (void)store.add_entry(make);

    Entry model;
    model.key   = make_exif_tag_key(store.arena(), "ifd0", 0x0110);
    model.value = make_text(store.arena(), "EOS R6", TextEncoding::Ascii);
    model.origin.block          = block;
    model.origin.order_in_block = 1;
    (void)store.add_entry(model);

    store.finalize();

    XmpSidecarRequest request;
    request.format               = XmpSidecarFormat::Portable;
    request.initial_output_bytes = 32U;

    std::vector<std::byte> out_a;
    std::vector<std::byte> out_b;
    const XmpDumpResult ra = dump_xmp_sidecar(store, &out_a, request);
    const XmpDumpResult rb = dump_xmp_sidecar(store, &out_b, request);

    ASSERT_EQ(ra.status, XmpDumpStatus::Ok);
    ASSERT_EQ(rb.status, XmpDumpStatus::Ok);
    ASSERT_EQ(ra.entries, rb.entries);
    ASSERT_EQ(ra.written, rb.written);
    ASSERT_EQ(out_a, out_b);
}


TEST(XmpDump, SidecarRequestApiRespectsOutputLimit)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry e;
    e.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    e.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    e.origin.block = block;
    e.origin.order_in_block = 0;
    (void)store.add_entry(e);
    store.finalize();

    XmpSidecarRequest request;
    request.format                  = XmpSidecarFormat::Lossless;
    request.initial_output_bytes    = 32U;
    request.limits.max_output_bytes = 32U;

    std::vector<std::byte> out;
    const XmpDumpResult r = dump_xmp_sidecar(store, &out, request);
    ASSERT_EQ(r.status, XmpDumpStatus::LimitExceeded);
}


TEST(XmpDump, SidecarApiClampsInitialOutputToConfiguredLimit)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry e;
    e.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    e.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    e.origin.block = block;
    e.origin.order_in_block = 0;
    (void)store.add_entry(e);
    store.finalize();

    XmpSidecarOptions options;
    options.format                           = XmpSidecarFormat::Lossless;
    options.initial_output_bytes             = 4096U;
    options.lossless.limits.max_output_bytes = 32U;

    std::vector<std::byte> out;
    const XmpDumpResult r = dump_xmp_sidecar(store, &out, options);
    ASSERT_EQ(r.status, XmpDumpStatus::LimitExceeded);
    EXPECT_EQ(out.size(), 32U);
}


TEST(XmpDump, SidecarRequestPortableEscapesUnsafeText)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    std::string unsafe;
    unsafe.push_back('A');
    unsafe.push_back(static_cast<char>(0x01));
    unsafe.append("<>&\"'");

    Entry xmp_label;
    xmp_label.key   = make_xmp_property_key(store.arena(),
                                            "http://ns.adobe.com/xap/1.0/",
                                            "Label");
    xmp_label.value = make_text(store.arena(), unsafe, TextEncoding::Utf8);
    xmp_label.origin.block          = block;
    xmp_label.origin.order_in_block = 0;
    (void)store.add_entry(xmp_label);
    store.finalize();

    XmpSidecarRequest request;
    request.format               = XmpSidecarFormat::Portable;
    request.initial_output_bytes = 64U;
    request.include_exif         = false;
    request.include_existing_xmp = true;

    std::vector<std::byte> out;
    const XmpDumpResult r = dump_xmp_sidecar(store, &out, request);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             out.size());
    EXPECT_NE(s.find("<xmp:Label>A\\x01&lt;&gt;&amp;&quot;&apos;</xmp:Label>"),
              std::string_view::npos);
}


TEST(XmpDump, EmitsPortablePacketWithExifAndTiff)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry make;
    make.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    make.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    make.origin.block = block;
    make.origin.order_in_block = 0;
    (void)store.add_entry(make);

    Entry exp;
    exp.key          = make_exif_tag_key(store.arena(), "exififd", 0x829A);
    exp.value        = make_urational(1, 1250);
    exp.origin.block = block;
    exp.origin.order_in_block = 1;
    (void)store.add_entry(exp);

    store.finalize();

    XmpPortableOptions opts;
    std::vector<std::byte> out(64);

    XmpDumpResult r1
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r1.status, XmpDumpStatus::OutputTruncated);
    ASSERT_GT(r1.needed, out.size());

    out.resize(static_cast<size_t>(r1.needed));
    const XmpDumpResult r2
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r2.status, XmpDumpStatus::Ok);
    ASSERT_GE(r2.entries, 2U);
    ASSERT_EQ(r2.written, r2.needed);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r2.written));
    EXPECT_NE(s.find("http://ns.adobe.com/exif/1.0/"), std::string_view::npos);
    EXPECT_NE(s.find("http://ns.adobe.com/tiff/1.0/"), std::string_view::npos);
    EXPECT_NE(s.find("<tiff:Make>Canon</tiff:Make>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExposureTime>1/1250</exif:ExposureTime>"),
              std::string_view::npos);
}

TEST(XmpDump, EmitsExrTypeNameInLosslessDump)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry e;
    e.key = make_exr_attribute_key(store.arena(), 0, "customA");
    const std::array<std::byte, 3> raw { std::byte { 0xAA }, std::byte { 0xBB },
                                         std::byte { 0xCC } };
    e.value                 = make_bytes(store.arena(),
                                         std::span<const std::byte>(raw.data(), raw.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    e.origin.wire_type      = WireType { WireFamily::Other, 31U };
    e.origin.wire_count     = 3U;
    e.origin.wire_type_name = store.arena().append_string("myVendorFoo");
    (void)store.add_entry(e);

    store.finalize();

    XmpDumpOptions opts;
    std::vector<std::byte> out(2048);
    const XmpDumpResult r
        = dump_xmp_lossless(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("exr:part:0:customA"), std::string_view::npos);
    EXPECT_NE(s.find("<omd:exrTypeName>myVendorFoo</omd:exrTypeName>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableIncludeExistingXmpSwitch)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry exif_make;
    exif_make.key   = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    exif_make.value = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    exif_make.origin.block          = block;
    exif_make.origin.order_in_block = 0;
    (void)store.add_entry(exif_make);

    Entry xmp_rating;
    xmp_rating.key                   = make_xmp_property_key(store.arena(),
                                                             "http://ns.adobe.com/xap/1.0/",
                                                             "Rating");
    xmp_rating.value                 = make_u16(5);
    xmp_rating.origin.block          = block;
    xmp_rating.origin.order_in_block = 1;
    (void)store.add_entry(xmp_rating);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(1024);
    XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 0U);

    std::string_view s(reinterpret_cast<const char*>(out.data()),
                       static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("<tiff:Make>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmp:Rating>"), std::string_view::npos);

    opts.include_existing_xmp = true;
    r = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                          opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    s = std::string_view(reinterpret_cast<const char*>(out.data()),
                         static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("<tiff:Make>"), std::string_view::npos);
    EXPECT_NE(s.find("<xmp:Rating>5</xmp:Rating>"), std::string_view::npos);
}


TEST(XmpDump, PortableExistingXmpIndexedPathEmitsSeq)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry li1;
    li1.key          = make_xmp_property_key(store.arena(),
                                             "http://ns.adobe.com/exif/1.0/",
                                             "GPSLatitude[1]");
    li1.value        = make_text(store.arena(), "41", TextEncoding::Ascii);
    li1.origin.block = block;
    li1.origin.order_in_block = 0;
    (void)store.add_entry(li1);

    Entry li2;
    li2.key          = make_xmp_property_key(store.arena(),
                                             "http://ns.adobe.com/exif/1.0/",
                                             "GPSLatitude[2]");
    li2.value        = make_text(store.arena(), "24", TextEncoding::Ascii);
    li2.origin.block = block;
    li2.origin.order_in_block = 1;
    (void)store.add_entry(li2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(1024);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<exif:GPSLatitude>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>41</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>24</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortableExistingXmpSubjectIndexedPathEmitsBag)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry li1;
    li1.key = make_xmp_property_key(store.arena(),
                                    "http://purl.org/dc/elements/1.1/",
                                    "subject[1]");
    li1.value = make_text(store.arena(), "travel", TextEncoding::Utf8);
    li1.origin.block          = block;
    li1.origin.order_in_block = 0;
    (void)store.add_entry(li1);

    Entry li2;
    li2.key = make_xmp_property_key(store.arena(),
                                    "http://purl.org/dc/elements/1.1/",
                                    "subject[2]");
    li2.value = make_text(store.arena(), "museum", TextEncoding::Utf8);
    li2.origin.block          = block;
    li2.origin.order_in_block = 1;
    (void)store.add_entry(li2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(1024);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:subject>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>travel</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>museum</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortableExistingXmpLanguageIndexedPathEmitsBag)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry li1;
    li1.key = make_xmp_property_key(store.arena(),
                                    "http://purl.org/dc/elements/1.1/",
                                    "language[1]");
    li1.value = make_text(store.arena(), "en", TextEncoding::Utf8);
    li1.origin.block          = block;
    li1.origin.order_in_block = 0;
    (void)store.add_entry(li1);

    Entry li2;
    li2.key = make_xmp_property_key(store.arena(),
                                    "http://purl.org/dc/elements/1.1/",
                                    "language[2]");
    li2.value = make_text(store.arena(), "ja", TextEncoding::Utf8);
    li2.origin.block          = block;
    li2.origin.order_in_block = 1;
    (void)store.add_entry(li2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(1024);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:language>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>en</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>ja</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortableExistingXmpDateIndexedPathEmitsSeq)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry li1;
    li1.key = make_xmp_property_key(store.arena(),
                                    "http://purl.org/dc/elements/1.1/",
                                    "date[1]");
    li1.value = make_text(store.arena(), "2026-04-01T10:00:00",
                          TextEncoding::Utf8);
    li1.origin.block          = block;
    li1.origin.order_in_block = 0;
    (void)store.add_entry(li1);

    Entry li2;
    li2.key = make_xmp_property_key(store.arena(),
                                    "http://purl.org/dc/elements/1.1/",
                                    "date[2]");
    li2.value = make_text(store.arena(), "2026-04-02T11:30:00",
                          TextEncoding::Utf8);
    li2.origin.block          = block;
    li2.origin.order_in_block = 1;
    (void)store.add_entry(li2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(1024);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:date>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>2026-04-01T10:00:00</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>2026-04-02T11:30:00</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableExistingXmpIdentifierIndexedPathEmitsBag)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry li1;
    li1.key = make_xmp_property_key(store.arena(),
                                    "http://ns.adobe.com/xap/1.0/",
                                    "Identifier[1]");
    li1.value = make_text(store.arena(), "urn:om:test:1",
                          TextEncoding::Utf8);
    li1.origin.block          = block;
    li1.origin.order_in_block = 0;
    (void)store.add_entry(li1);

    Entry li2;
    li2.key = make_xmp_property_key(store.arena(),
                                    "http://ns.adobe.com/xap/1.0/",
                                    "Identifier[2]");
    li2.value = make_text(store.arena(), "urn:om:test:2",
                          TextEncoding::Utf8);
    li2.origin.block          = block;
    li2.origin.order_in_block = 1;
    (void)store.add_entry(li2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(1024);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<xmp:Identifier>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>urn:om:test:1</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>urn:om:test:2</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableExistingXmpAdvisoryIndexedPathEmitsBag)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry li1;
    li1.key = make_xmp_property_key(store.arena(),
                                    "http://ns.adobe.com/xap/1.0/",
                                    "Advisory[1]");
    li1.value = make_text(store.arena(), "xmp:MetadataDate",
                          TextEncoding::Utf8);
    li1.origin.block          = block;
    li1.origin.order_in_block = 0;
    (void)store.add_entry(li1);

    Entry li2;
    li2.key = make_xmp_property_key(store.arena(),
                                    "http://ns.adobe.com/xap/1.0/",
                                    "Advisory[2]");
    li2.value = make_text(store.arena(), "photoshop:City",
                          TextEncoding::Utf8);
    li2.origin.block          = block;
    li2.origin.order_in_block = 1;
    (void)store.add_entry(li2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(1024);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<xmp:Advisory>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>xmp:MetadataDate</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>photoshop:City</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableExistingXmpRightsOwnerIndexedPathEmitsBag)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry li1;
    li1.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/", "Owner[1]");
    li1.value = make_text(store.arena(), "OpenMeta Labs",
                          TextEncoding::Utf8);
    li1.origin.block          = block;
    li1.origin.order_in_block = 0;
    (void)store.add_entry(li1);

    Entry li2;
    li2.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/", "Owner[2]");
    li2.value = make_text(store.arena(), "Example Archive",
                          TextEncoding::Utf8);
    li2.origin.block          = block;
    li2.origin.order_in_block = 1;
    (void)store.add_entry(li2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(1024);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<xmpRights:Owner>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>OpenMeta Labs</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Example Archive</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableExistingXmpTitleAltTextEmitsRdfAlt)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry xmp_title_default;
    xmp_title_default.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "title[@xml:lang=x-default]");
    xmp_title_default.value = make_text(store.arena(), "Default title",
                                        TextEncoding::Utf8);
    xmp_title_default.origin.block          = block;
    xmp_title_default.origin.order_in_block = 0;
    (void)store.add_entry(xmp_title_default);

    Entry xmp_title_fr;
    xmp_title_fr.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "title[@xml:lang=fr-FR]");
    xmp_title_fr.value = make_text(store.arena(), "Titre",
                                   TextEncoding::Utf8);
    xmp_title_fr.origin.block          = block;
    xmp_title_fr.origin.order_in_block = 1;
    (void)store.add_entry(xmp_title_fr);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(2048);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:title>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Alt>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Default title</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"fr-FR\">Titre</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableDeduplicatesSamePropertyName)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry width0;
    width0.key          = make_exif_tag_key(store.arena(), "ifd0", 0x0100);
    width0.value        = make_u32(5184);
    width0.origin.block = block;
    width0.origin.order_in_block = 0;
    (void)store.add_entry(width0);

    Entry width1;
    width1.key          = make_exif_tag_key(store.arena(), "ifd1", 0x0100);
    width1.value        = make_u32(668);
    width1.origin.block = block;
    width1.origin.order_in_block = 1;
    (void)store.add_entry(width1);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(2048);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_EQ(count_substring(s, "<tiff:ImageWidth>"), 1U);
    EXPECT_NE(s.find("<tiff:ImageWidth>5184</tiff:ImageWidth>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableUsesCanonicalXmpPropertyNames)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry image_length;
    image_length.key   = make_exif_tag_key(store.arena(), "ifd0", 0x0101);
    image_length.value = make_u32(3456);
    image_length.origin.block          = block;
    image_length.origin.order_in_block = 0;
    (void)store.add_entry(image_length);

    Entry exposure_bias;
    exposure_bias.key   = make_exif_tag_key(store.arena(), "exififd", 0x9204);
    exposure_bias.value = make_srational(0, 1);
    exposure_bias.origin.block          = block;
    exposure_bias.origin.order_in_block = 1;
    (void)store.add_entry(exposure_bias);

    Entry iso;
    iso.key          = make_exif_tag_key(store.arena(), "exififd", 0x8827);
    iso.value        = make_u16(400);
    iso.origin.block = block;
    iso.origin.order_in_block = 2;
    (void)store.add_entry(iso);

    Entry pixel_x;
    pixel_x.key          = make_exif_tag_key(store.arena(), "exififd", 0xA002);
    pixel_x.value        = make_u16(6000);
    pixel_x.origin.block = block;
    pixel_x.origin.order_in_block = 3;
    (void)store.add_entry(pixel_x);

    Entry pixel_y;
    pixel_y.key          = make_exif_tag_key(store.arena(), "exififd", 0xA003);
    pixel_y.value        = make_u16(4000);
    pixel_y.origin.block = block;
    pixel_y.origin.order_in_block = 4;
    (void)store.add_entry(pixel_y);

    Entry focal35;
    focal35.key          = make_exif_tag_key(store.arena(), "exififd", 0xA405);
    focal35.value        = make_u16(50);
    focal35.origin.block = block;
    focal35.origin.order_in_block = 5;
    (void)store.add_entry(focal35);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<tiff:ImageHeight>3456</tiff:ImageHeight>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExposureCompensation>0</exif:ExposureCompensation>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ISO>400</exif:ISO>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExifImageWidth>6000</exif:ExifImageWidth>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExifImageHeight>4000</exif:ExifImageHeight>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<exif:FocalLengthIn35mmFormat>50</exif:FocalLengthIn35mmFormat>"),
        std::string_view::npos);

    EXPECT_EQ(s.find("<tiff:ImageLength>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ExposureBiasValue>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ISOSpeedRatings>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:PixelXDimension>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:PixelYDimension>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:FocalLengthIn35mmFilm>"), std::string_view::npos);
}

TEST(XmpDump, PortableNormalizesRationalAndSkipsXmlPacket)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry xres;
    xres.key          = make_exif_tag_key(store.arena(), "ifd0", 0x011A);
    xres.value        = make_urational(72, 1);
    xres.origin.block = block;
    xres.origin.order_in_block = 0;
    (void)store.add_entry(xres);

    Entry exp_comp;
    exp_comp.key          = make_exif_tag_key(store.arena(), "exififd", 0x9204);
    exp_comp.value        = make_srational(10, 20);
    exp_comp.origin.block = block;
    exp_comp.origin.order_in_block = 1;
    (void)store.add_entry(exp_comp);

    Entry xml_packet;
    xml_packet.key          = make_exif_tag_key(store.arena(), "ifd0", 0x02BC);
    xml_packet.value        = make_text(store.arena(), "<x:xmpmeta/>",
                                        TextEncoding::Ascii);
    xml_packet.origin.block = block;
    xml_packet.origin.order_in_block = 2;
    (void)store.add_entry(xml_packet);

    Entry maker_note;
    maker_note.key = make_exif_tag_key(store.arena(), "exififd", 0x927C);
    const std::array<std::byte, 4> maker_note_bytes = {
        std::byte { 0xDE },
        std::byte { 0xAD },
        std::byte { 0xBE },
        std::byte { 0xEF },
    };
    maker_note.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(maker_note_bytes.data(),
                                                maker_note_bytes.size()));
    maker_note.origin.block          = block;
    maker_note.origin.order_in_block = 3;
    (void)store.add_entry(maker_note);

    Entry dng_private;
    dng_private.key = make_exif_tag_key(store.arena(), "ifd0", 0xC634);
    const std::array<std::byte, 4> dng_private_bytes = {
        std::byte { 0xAA },
        std::byte { 0xBB },
        std::byte { 0xCC },
        std::byte { 0xDD },
    };
    dng_private.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(dng_private_bytes.data(),
                                                dng_private_bytes.size()));
    dng_private.origin.block          = block;
    dng_private.origin.order_in_block = 4;
    (void)store.add_entry(dng_private);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<tiff:XResolution>72</tiff:XResolution>"),
              std::string_view::npos);
    EXPECT_NE(s.find(
                  "<exif:ExposureCompensation>0.5</exif:ExposureCompensation>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:XMLPacket>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:MakerNote>"), std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:DNGPrivateData>"), std::string_view::npos);
}

TEST(XmpDump, PortableNormalizesExifDateTagsToXmpDateTime)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry dt;
    dt.key          = make_exif_tag_key(store.arena(), "ifd0", 0x0132);
    dt.value        = make_text(store.arena(), "2010-11-14 16:25:16 UTC",
                                TextEncoding::Ascii);
    dt.origin.block = block;
    dt.origin.order_in_block = 0;
    (void)store.add_entry(dt);

    Entry dto;
    dto.key          = make_exif_tag_key(store.arena(), "exififd", 0x9003);
    dto.value        = make_text(store.arena(), "2010:11:14 16:25:16",
                                 TextEncoding::Ascii);
    dto.origin.block = block;
    dto.origin.order_in_block = 1;
    (void)store.add_entry(dto);

    Entry pdt;
    pdt.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC71B);
    pdt.value        = make_text(store.arena(), "2010:11:14 16:25:16+0900",
                                 TextEncoding::Ascii);
    pdt.origin.block = block;
    pdt.origin.order_in_block = 2;
    (void)store.add_entry(pdt);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<tiff:DateTime>2010-11-14T16:25:16Z</tiff:DateTime>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<exif:DateTimeOriginal>2010-11-14T16:25:16</exif:DateTimeOriginal>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<tiff:PreviewDateTime>2010-11-14T16:25:16+09:00</tiff:PreviewDateTime>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmp:ModifyDate>2010-11-14T16:25:16Z</xmp:ModifyDate>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableGeneratesXmpCreateDateAliasFromExifDigitizedTime)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry dtd;
    dtd.key          = make_exif_tag_key(store.arena(), "exififd", 0x9004);
    dtd.value        = make_text(store.arena(), "2010:11:14 16:25:16",
                                 TextEncoding::Ascii);
    dtd.origin.block = block;
    dtd.origin.order_in_block = 0;
    (void)store.add_entry(dtd);
    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find(
            "<exif:DateTimeDigitized>2010-11-14T16:25:16</exif:DateTimeDigitized>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmp:CreateDate>2010-11-14T16:25:16</xmp:CreateDate>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesManagedXmpDateAliasesOnlyWhenAvailable)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry dt;
    dt.key          = make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    dt.value        = make_text(store.arena(), "2010:11:14 16:25:16",
                                TextEncoding::Ascii);
    dt.origin.block = block;
    dt.origin.order_in_block = 0;
    (void)store.add_entry(dt);

    Entry xmp_modify_date;
    xmp_modify_date.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "ModifyDate");
    xmp_modify_date.value = make_text(store.arena(), "1999-01-02T03:04:05",
                                      TextEncoding::Utf8);
    xmp_modify_date.origin.block          = block;
    xmp_modify_date.origin.order_in_block = 1;
    (void)store.add_entry(xmp_modify_date);

    Entry xmp_create_date;
    xmp_create_date.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreateDate");
    xmp_create_date.value = make_text(store.arena(), "1980-01-02T03:04:05",
                                      TextEncoding::Utf8);
    xmp_create_date.origin.block          = block;
    xmp_create_date.origin.order_in_block = 2;
    (void)store.add_entry(xmp_create_date);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<xmp:ModifyDate>2010-11-14T16:25:16</xmp:ModifyDate>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmp:ModifyDate>1999-01-02T03:04:05</xmp:ModifyDate>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmp:CreateDate>1980-01-02T03:04:05</xmp:CreateDate>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableDecodesWindowsXpTextTagsAsUtf8Text)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::vector<std::byte> keywords = make_utf16le_ascii_bytes(
        "alpha;beta");
    Entry xp_keywords;
    xp_keywords.key          = make_exif_tag_key(store.arena(), "ifd0", 0x9C9E);
    xp_keywords.value        = make_array(store.arena(), MetaElementType::U8,
                                          std::span<const std::byte>(keywords.data(),
                                                                     keywords.size()),
                                          1U);
    xp_keywords.origin.block = block;
    xp_keywords.origin.order_in_block = 0;
    (void)store.add_entry(xp_keywords);

    const std::array<std::byte, 8> empty_subject = {
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 },
    };
    Entry xp_subject;
    xp_subject.key = make_exif_tag_key(store.arena(), "ifd0", 0x9C9F);
    xp_subject.value
        = make_array(store.arena(), MetaElementType::U8,
                     std::span<const std::byte>(empty_subject.data(),
                                                empty_subject.size()),
                     1U);
    xp_subject.origin.block          = block;
    xp_subject.origin.order_in_block = 1;
    (void)store.add_entry(xp_subject);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<tiff:XPKeywords>alpha;beta</tiff:XPKeywords>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<tiff:XPSubject></tiff:XPSubject>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:Seq>"), std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesExistingXmpPropertyNames)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry image_length;
    image_length.key                   = make_xmp_property_key(store.arena(),
                                                               "http://ns.adobe.com/tiff/1.0/",
                                                               "ImageLength");
    image_length.value                 = make_u32(3456);
    image_length.origin.block          = block;
    image_length.origin.order_in_block = 0;
    (void)store.add_entry(image_length);

    Entry exposure_bias;
    exposure_bias.key                   = make_xmp_property_key(store.arena(),
                                                                "http://ns.adobe.com/exif/1.0/",
                                                                "ExposureBiasValue");
    exposure_bias.value                 = make_u16(0);
    exposure_bias.origin.block          = block;
    exposure_bias.origin.order_in_block = 1;
    (void)store.add_entry(exposure_bias);

    Entry iso;
    iso.key                   = make_xmp_property_key(store.arena(),
                                                      "http://ns.adobe.com/exif/1.0/",
                                                      "ISOSpeedRatings");
    iso.value                 = make_u16(400);
    iso.origin.block          = block;
    iso.origin.order_in_block = 2;
    (void)store.add_entry(iso);

    Entry pixel_x;
    pixel_x.key                   = make_xmp_property_key(store.arena(),
                                                          "http://ns.adobe.com/exif/1.0/",
                                                          "PixelXDimension");
    pixel_x.value                 = make_u16(6000);
    pixel_x.origin.block          = block;
    pixel_x.origin.order_in_block = 3;
    (void)store.add_entry(pixel_x);

    Entry pixel_y;
    pixel_y.key                   = make_xmp_property_key(store.arena(),
                                                          "http://ns.adobe.com/exif/1.0/",
                                                          "PixelYDimension");
    pixel_y.value                 = make_u16(4000);
    pixel_y.origin.block          = block;
    pixel_y.origin.order_in_block = 4;
    (void)store.add_entry(pixel_y);

    Entry focal35;
    focal35.key                   = make_xmp_property_key(store.arena(),
                                                          "http://ns.adobe.com/exif/1.0/",
                                                          "FocalLengthIn35mmFilm");
    focal35.value                 = make_u16(50);
    focal35.origin.block          = block;
    focal35.origin.order_in_block = 5;
    (void)store.add_entry(focal35);

    Entry maker_note;
    maker_note.key   = make_xmp_property_key(store.arena(),
                                             "http://ns.adobe.com/exif/1.0/",
                                             "MakerNote");
    maker_note.value = make_text(store.arena(), "AAAA", TextEncoding::Ascii);
    maker_note.origin.block          = block;
    maker_note.origin.order_in_block = 6;
    (void)store.add_entry(maker_note);

    Entry dng_private;
    dng_private.key   = make_xmp_property_key(store.arena(),
                                              "http://ns.adobe.com/tiff/1.0/",
                                              "DNGPrivateData");
    dng_private.value = make_text(store.arena(), "BBBB", TextEncoding::Ascii);
    dng_private.origin.block          = block;
    dng_private.origin.order_in_block = 7;
    (void)store.add_entry(dng_private);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 6U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<tiff:ImageHeight>3456</tiff:ImageHeight>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExposureCompensation>0</exif:ExposureCompensation>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ISO>400</exif:ISO>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExifImageWidth>6000</exif:ExifImageWidth>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExifImageHeight>4000</exif:ExifImageHeight>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<exif:FocalLengthIn35mmFormat>50</exif:FocalLengthIn35mmFormat>"),
        std::string_view::npos);

    EXPECT_EQ(s.find("<tiff:ImageLength>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ExposureBiasValue>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ISOSpeedRatings>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:PixelXDimension>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:PixelYDimension>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:FocalLengthIn35mmFilm>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:MakerNote>"), std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:DNGPrivateData>"), std::string_view::npos);
}

TEST(XmpDump, PortablePrintConvertsCommonExifEnumsAndValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry orientation;
    orientation.key          = make_exif_tag_key(store.arena(), "ifd0", 0x0112);
    orientation.value        = make_u16(6);
    orientation.origin.block = block;
    orientation.origin.order_in_block = 0;
    (void)store.add_entry(orientation);

    Entry resolution_unit;
    resolution_unit.key   = make_exif_tag_key(store.arena(), "ifd0", 0x0128);
    resolution_unit.value = make_u16(2);
    resolution_unit.origin.block          = block;
    resolution_unit.origin.order_in_block = 1;
    (void)store.add_entry(resolution_unit);

    Entry metering_mode;
    metering_mode.key   = make_exif_tag_key(store.arena(), "exififd", 0x9207);
    metering_mode.value = make_u16(5);
    metering_mode.origin.block          = block;
    metering_mode.origin.order_in_block = 2;
    (void)store.add_entry(metering_mode);

    Entry exposure_program;
    exposure_program.key = make_exif_tag_key(store.arena(), "exififd", 0x8822);
    exposure_program.value                 = make_u16(2);
    exposure_program.origin.block          = block;
    exposure_program.origin.order_in_block = 3;
    (void)store.add_entry(exposure_program);

    Entry focal_length;
    focal_length.key   = make_exif_tag_key(store.arena(), "exififd", 0x920A);
    focal_length.value = make_urational(66, 1);
    focal_length.origin.block          = block;
    focal_length.origin.order_in_block = 4;
    (void)store.add_entry(focal_length);

    Entry shutter_speed;
    shutter_speed.key   = make_exif_tag_key(store.arena(), "exififd", 0x9201);
    shutter_speed.value = make_srational(6, 1);
    shutter_speed.origin.block          = block;
    shutter_speed.origin.order_in_block = 5;
    (void)store.add_entry(shutter_speed);

    const std::array<URational, 4> lens_spec = {
        URational { 24, 1 },
        URational { 70, 1 },
        URational { 0, 1 },
        URational { 0, 1 },
    };
    Entry lens_spec_entry;
    lens_spec_entry.key = make_exif_tag_key(store.arena(), "exififd", 0xA432);
    lens_spec_entry.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(lens_spec.data(),
                                                          lens_spec.size()));
    lens_spec_entry.origin.block          = block;
    lens_spec_entry.origin.order_in_block = 6;
    (void)store.add_entry(lens_spec_entry);

    const std::array<uint8_t, 4> gps_ver = { 2, 3, 0, 0 };
    Entry gps_version;
    gps_version.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0000);
    gps_version.value = make_u8_array(store.arena(),
                                      std::span<const uint8_t>(gps_ver.data(),
                                                               gps_ver.size()));
    gps_version.origin.block          = block;
    gps_version.origin.order_in_block = 7;
    (void)store.add_entry(gps_version);

    Entry gps_lat_ref;
    gps_lat_ref.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0001);
    gps_lat_ref.value = make_text(store.arena(), "N", TextEncoding::Ascii);
    gps_lat_ref.origin.block          = block;
    gps_lat_ref.origin.order_in_block = 8;
    (void)store.add_entry(gps_lat_ref);

    const std::array<URational, 3> gps_lat_triplet = {
        URational { 41, 1 },
        URational { 24, 1 },
        URational { 30, 1 },
    };
    Entry gps_lat;
    gps_lat.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0002);
    gps_lat.value = make_urational_array(
        store.arena(), std::span<const URational>(gps_lat_triplet.data(),
                                                  gps_lat_triplet.size()));
    gps_lat.origin.block          = block;
    gps_lat.origin.order_in_block = 9;
    (void)store.add_entry(gps_lat);

    Entry gps_lon_ref;
    gps_lon_ref.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0003);
    gps_lon_ref.value = make_text(store.arena(), "E", TextEncoding::Ascii);
    gps_lon_ref.origin.block          = block;
    gps_lon_ref.origin.order_in_block = 10;
    (void)store.add_entry(gps_lon_ref);

    const std::array<URational, 3> gps_lon_triplet = {
        URational { 2, 1 },
        URational { 9, 1 },
        URational { 0, 1 },
    };
    Entry gps_lon;
    gps_lon.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0004);
    gps_lon.value = make_urational_array(
        store.arena(), std::span<const URational>(gps_lon_triplet.data(),
                                                  gps_lon_triplet.size()));
    gps_lon.origin.block          = block;
    gps_lon.origin.order_in_block = 11;
    (void)store.add_entry(gps_lon);

    Entry gps_date;
    gps_date.key          = make_exif_tag_key(store.arena(), "gpsifd", 0x001D);
    gps_date.value        = make_text(store.arena(), "2024:04:19",
                                      TextEncoding::Ascii);
    gps_date.origin.block = block;
    gps_date.origin.order_in_block = 12;
    (void)store.add_entry(gps_date);

    const std::array<URational, 3> gps_time_triplet = {
        URational { 12, 1 },
        URational { 11, 1 },
        URational { 13, 1 },
    };
    Entry gps_time;
    gps_time.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0007);
    gps_time.value = make_urational_array(
        store.arena(), std::span<const URational>(gps_time_triplet.data(),
                                                  gps_time_triplet.size()));
    gps_time.origin.block          = block;
    gps_time.origin.order_in_block = 13;
    (void)store.add_entry(gps_time);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));

    EXPECT_NE(s.find("<tiff:Orientation>6</tiff:Orientation>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<tiff:ResolutionUnit>2</tiff:ResolutionUnit>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:MeteringMode>5</exif:MeteringMode>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExposureProgram>2</exif:ExposureProgram>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:FocalLength>66.0 mm</exif:FocalLength>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ShutterSpeedValue>1/64</exif:ShutterSpeedValue>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSVersionID>2</exif:GPSVersionID>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSLatitude>41,24.5N</exif:GPSLatitude>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSLongitude>2,9E</exif:GPSLongitude>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<exif:GPSTimeStamp>2024-04-19T12:11:13Z</exif:GPSTimeStamp>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>24</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>70</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortableSkipsInvalidGpsRationalValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry gps_lat_ref;
    gps_lat_ref.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0001);
    gps_lat_ref.value = make_text(store.arena(), "N", TextEncoding::Ascii);
    gps_lat_ref.origin.block          = block;
    gps_lat_ref.origin.order_in_block = 0;
    (void)store.add_entry(gps_lat_ref);

    const std::array<URational, 3> invalid_triplet = {
        URational { 0, 0 },
        URational { 0, 0 },
        URational { 0, 0 },
    };
    Entry gps_lat;
    gps_lat.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0002);
    gps_lat.value = make_urational_array(
        store.arena(), std::span<const URational>(invalid_triplet.data(),
                                                  invalid_triplet.size()));
    gps_lat.origin.block          = block;
    gps_lat.origin.order_in_block = 1;
    (void)store.add_entry(gps_lat);

    Entry gps_time;
    gps_time.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0007);
    gps_time.value = make_urational_array(
        store.arena(), std::span<const URational>(invalid_triplet.data(),
                                                  invalid_triplet.size()));
    gps_time.origin.block          = block;
    gps_time.origin.order_in_block = 2;
    (void)store.add_entry(gps_time);

    Entry gps_alt;
    gps_alt.key          = make_exif_tag_key(store.arena(), "gpsifd", 0x0006);
    gps_alt.value        = make_urational(0, 0);
    gps_alt.origin.block = block;
    gps_alt.origin.order_in_block = 3;
    (void)store.add_entry(gps_alt);

    Entry gps_img_dir;
    gps_img_dir.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0011);
    gps_img_dir.value = make_urational(42889, 241);
    gps_img_dir.origin.block          = block;
    gps_img_dir.origin.order_in_block = 4;
    (void)store.add_entry(gps_img_dir);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<exif:GPSLatitudeRef>N</exif:GPSLatitudeRef>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSImgDirection>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:GPSLatitude>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:GPSTimeStamp>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:GPSAltitude>"), std::string_view::npos);
}


TEST(XmpDump, PortableEmitsGpsDestinationCoordsAndAltitudeFromArrays)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry gps_dest_lat_ref;
    gps_dest_lat_ref.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0013);
    gps_dest_lat_ref.value = make_text(store.arena(), "N", TextEncoding::Ascii);
    gps_dest_lat_ref.origin.block          = block;
    gps_dest_lat_ref.origin.order_in_block = 0;
    (void)store.add_entry(gps_dest_lat_ref);

    const std::array<URational, 3> gps_dest_lat = {
        URational { 35, 1 },
        URational { 48, 1 },
        URational { 8, 1 },
    };
    Entry gps_dest_lat_entry;
    gps_dest_lat_entry.key = make_exif_tag_key(store.arena(), "gpsifd", 0x0014);
    gps_dest_lat_entry.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(gps_dest_lat.data(),
                                                          gps_dest_lat.size()));
    gps_dest_lat_entry.origin.block          = block;
    gps_dest_lat_entry.origin.order_in_block = 1;
    (void)store.add_entry(gps_dest_lat_entry);

    Entry gps_dest_lon_ref;
    gps_dest_lon_ref.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0015);
    gps_dest_lon_ref.value = make_text(store.arena(), "E", TextEncoding::Ascii);
    gps_dest_lon_ref.origin.block          = block;
    gps_dest_lon_ref.origin.order_in_block = 2;
    (void)store.add_entry(gps_dest_lon_ref);

    const std::array<URational, 3> gps_dest_lon = {
        URational { 139, 1 },
        URational { 34, 1 },
        URational { 55, 1 },
    };
    Entry gps_dest_lon_entry;
    gps_dest_lon_entry.key = make_exif_tag_key(store.arena(), "gpsifd", 0x0016);
    gps_dest_lon_entry.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(gps_dest_lon.data(),
                                                          gps_dest_lon.size()));
    gps_dest_lon_entry.origin.block          = block;
    gps_dest_lon_entry.origin.order_in_block = 3;
    (void)store.add_entry(gps_dest_lon_entry);

    const std::array<URational, 3> gps_altitude = {
        URational { 277, 1 },
        URational { 58, 1 },
        URational { 25, 1 },
    };
    Entry gps_alt;
    gps_alt.key = make_exif_tag_key(store.arena(), "gpsifd", 0x0006);
    gps_alt.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(gps_altitude.data(),
                                                          gps_altitude.size()));
    gps_alt.origin.block          = block;
    gps_alt.origin.order_in_block = 4;
    (void)store.add_entry(gps_alt);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<exif:GPSDestLatitude>35,48.13333333N</exif:GPSDestLatitude>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<exif:GPSDestLongitude>139,34.91666667E</exif:GPSDestLongitude>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSAltitude>277</exif:GPSAltitude>"),
              std::string_view::npos);
}


TEST(XmpDump, PortableSkipsMalformedNumericBlobExifTags)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::array<uint32_t, 4> user_comment_words = {
        5000U,
        5006U,
        116U,
        1U,
    };
    Entry user_comment;
    user_comment.key = make_exif_tag_key(store.arena(), "exififd", 0x9286);
    user_comment.value
        = make_u32_array(store.arena(),
                         std::span<const uint32_t>(user_comment_words.data(),
                                                   user_comment_words.size()));
    user_comment.origin.block          = block;
    user_comment.origin.order_in_block = 0;
    (void)store.add_entry(user_comment);

    const std::array<uint16_t, 4> device_setting_words = {
        5000U,
        5001U,
        55U,
        104U,
    };
    Entry device_setting;
    device_setting.key   = make_exif_tag_key(store.arena(), "exififd", 0xA40B);
    device_setting.value = make_u16_array(
        store.arena(), std::span<const uint16_t>(device_setting_words.data(),
                                                 device_setting_words.size()));
    device_setting.origin.block          = block;
    device_setting.origin.order_in_block = 1;
    (void)store.add_entry(device_setting);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("<exif:UserComment>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:DeviceSettingDescription>"),
              std::string_view::npos);
}


TEST(XmpDump, PortableSkipsInvalidApexRationalValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry fnumber;
    fnumber.key          = make_exif_tag_key(store.arena(), "exififd", 0x829D);
    fnumber.value        = make_urational(0, 0);
    fnumber.origin.block = block;
    fnumber.origin.order_in_block = 0;
    (void)store.add_entry(fnumber);

    Entry aperture;
    aperture.key          = make_exif_tag_key(store.arena(), "exififd", 0x9202);
    aperture.value        = make_urational(0, 0);
    aperture.origin.block = block;
    aperture.origin.order_in_block = 1;
    (void)store.add_entry(aperture);

    Entry shutter;
    shutter.key          = make_exif_tag_key(store.arena(), "exififd", 0x9201);
    shutter.value        = make_srational(0, 0);
    shutter.origin.block = block;
    shutter.origin.order_in_block = 2;
    (void)store.add_entry(shutter);

    Entry exposure_comp;
    exposure_comp.key   = make_exif_tag_key(store.arena(), "exififd", 0x9204);
    exposure_comp.value = make_srational(0, 0);
    exposure_comp.origin.block          = block;
    exposure_comp.origin.order_in_block = 3;
    (void)store.add_entry(exposure_comp);

    Entry focal_length;
    focal_length.key   = make_exif_tag_key(store.arena(), "exififd", 0x920A);
    focal_length.value = make_urational(66, 1);
    focal_length.origin.block          = block;
    focal_length.origin.order_in_block = 4;
    (void)store.add_entry(focal_length);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("<exif:FNumber>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ApertureValue>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ShutterSpeedValue>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ExposureCompensation>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:FocalLength>66.0 mm</exif:FocalLength>"),
              std::string_view::npos);
}


TEST(XmpDump, PortableApexTagsUseFirstValidArrayElement)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::array<URational, 2> fnumber_vals = {
        URational { 139, 50 },
        URational { 0, 0 },
    };
    Entry fnumber;
    fnumber.key = make_exif_tag_key(store.arena(), "exififd", 0x829D);
    fnumber.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(fnumber_vals.data(),
                                                          fnumber_vals.size()));
    fnumber.origin.block          = block;
    fnumber.origin.order_in_block = 0;
    (void)store.add_entry(fnumber);

    const std::array<URational, 2> aperture_vals = {
        URational { 0, 0 }, URational { 4, 1 },  // APEX 4 -> f/4.0
    };
    Entry aperture;
    aperture.key   = make_exif_tag_key(store.arena(), "exififd", 0x9202);
    aperture.value = make_urational_array(
        store.arena(),
        std::span<const URational>(aperture_vals.data(), aperture_vals.size()));
    aperture.origin.block          = block;
    aperture.origin.order_in_block = 1;
    (void)store.add_entry(aperture);

    const std::array<SRational, 2> shutter_vals = {
        SRational { 0, 0 }, SRational { 6, 1 },  // APEX 6 -> 1/64 s
    };
    Entry shutter;
    shutter.key = make_exif_tag_key(store.arena(), "exififd", 0x9201);
    shutter.value
        = make_srational_array(store.arena(),
                               std::span<const SRational>(shutter_vals.data(),
                                                          shutter_vals.size()));
    shutter.origin.block          = block;
    shutter.origin.order_in_block = 2;
    (void)store.add_entry(shutter);

    const std::array<SRational, 2> exp_comp_vals = {
        SRational { 0, 0 },
        SRational { 1, 2 },
    };
    Entry exp_comp;
    exp_comp.key   = make_exif_tag_key(store.arena(), "exififd", 0x9204);
    exp_comp.value = make_srational_array(
        store.arena(),
        std::span<const SRational>(exp_comp_vals.data(), exp_comp_vals.size()));
    exp_comp.origin.block          = block;
    exp_comp.origin.order_in_block = 3;
    (void)store.add_entry(exp_comp);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<exif:FNumber>2.8</exif:FNumber>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ApertureValue>4.0</exif:ApertureValue>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ShutterSpeedValue>1/64</exif:ShutterSpeedValue>"),
              std::string_view::npos);
    EXPECT_NE(s.find(
                  "<exif:ExposureCompensation>0.5</exif:ExposureCompensation>"),
              std::string_view::npos);
}


TEST(XmpDump, PortableSkipsAbsurdApexApertureValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry aperture;
    aperture.key          = make_exif_tag_key(store.arena(), "exififd", 0x9202);
    aperture.value        = make_urational(30, 1);  // 2^(15) = 32768.0
    aperture.origin.block = block;
    aperture.origin.order_in_block = 0;
    (void)store.add_entry(aperture);

    Entry max_aperture;
    max_aperture.key   = make_exif_tag_key(store.arena(), "exififd", 0x9205);
    max_aperture.value = make_urational(30, 1);
    max_aperture.origin.block          = block;
    max_aperture.origin.order_in_block = 1;
    (void)store.add_entry(max_aperture);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("<exif:ApertureValue>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:MaxApertureValue>"), std::string_view::npos);
}


TEST(XmpDump, PortableFormatsGpsCoordinatesFromSrationalValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry gps_lat_ref;
    gps_lat_ref.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0001);
    gps_lat_ref.value = make_text(store.arena(), "N", TextEncoding::Ascii);
    gps_lat_ref.origin.block          = block;
    gps_lat_ref.origin.order_in_block = 0;
    (void)store.add_entry(gps_lat_ref);

    const std::array<SRational, 3> gps_lat_vals = {
        SRational { 45, 1 },
        SRational { 0, 1 },
        SRational { 185806272, 10000000 },
    };
    Entry gps_lat;
    gps_lat.key = make_exif_tag_key(store.arena(), "gpsifd", 0x0002);
    gps_lat.value
        = make_srational_array(store.arena(),
                               std::span<const SRational>(gps_lat_vals.data(),
                                                          gps_lat_vals.size()));
    gps_lat.origin.block          = block;
    gps_lat.origin.order_in_block = 1;
    (void)store.add_entry(gps_lat);

    Entry gps_lon_ref;
    gps_lon_ref.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0003);
    gps_lon_ref.value = make_text(store.arena(), "W", TextEncoding::Ascii);
    gps_lon_ref.origin.block          = block;
    gps_lon_ref.origin.order_in_block = 2;
    (void)store.add_entry(gps_lon_ref);

    const std::array<SRational, 3> gps_lon_vals = {
        SRational { 93, 1 },
        SRational { 27, 1 },
        SRational { 411877440, 10000000 },
    };
    Entry gps_lon;
    gps_lon.key = make_exif_tag_key(store.arena(), "gpsifd", 0x0004);
    gps_lon.value
        = make_srational_array(store.arena(),
                               std::span<const SRational>(gps_lon_vals.data(),
                                                          gps_lon_vals.size()));
    gps_lon.origin.block          = block;
    gps_lon.origin.order_in_block = 3;
    (void)store.add_entry(gps_lon);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<exif:GPSLatitude>45,0.30967712N</exif:GPSLatitude>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSLongitude>93,27.6864624W</exif:GPSLongitude>"),
              std::string_view::npos);
}


TEST(XmpDump, PortableSkipsInvalidTiffAndDngRationalTags)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry xres;
    xres.key          = make_exif_tag_key(store.arena(), "ifd0", 0x011A);
    xres.value        = make_urational(0, 0);
    xres.origin.block = block;
    xres.origin.order_in_block = 0;
    (void)store.add_entry(xres);

    Entry yres;
    yres.key          = make_exif_tag_key(store.arena(), "ifd0", 0x011B);
    yres.value        = make_urational(0, 0);
    yres.origin.block = block;
    yres.origin.order_in_block = 1;
    (void)store.add_entry(yres);

    const std::array<URational, 2> crop_vals = {
        URational { 0, 0 },
        URational { 0, 0 },
    };
    Entry crop_size;
    crop_size.key = make_exif_tag_key(store.arena(), "ifd0", 0xC793);
    crop_size.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(crop_vals.data(),
                                                          crop_vals.size()));
    crop_size.origin.block          = block;
    crop_size.origin.order_in_block = 2;
    (void)store.add_entry(crop_size);

    Entry model;
    model.key   = make_exif_tag_key(store.arena(), "ifd0", 0x0110);
    model.value = make_text(store.arena(), "DNG Test", TextEncoding::Ascii);
    model.origin.block          = block;
    model.origin.order_in_block = 3;
    (void)store.add_entry(model);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("<tiff:XResolution>"), std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:YResolution>"), std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:OriginalDefaultCropSize>"), std::string_view::npos);
    EXPECT_NE(s.find("<tiff:Model>DNG Test</tiff:Model>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableSkipsInvalidUndefinedRationalTags)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry flash_energy;
    flash_energy.key   = make_exif_tag_key(store.arena(), "exififd", 0x920B);
    flash_energy.value = make_urational(0, 0);
    flash_energy.origin.block          = block;
    flash_energy.origin.order_in_block = 0;
    (void)store.add_entry(flash_energy);

    Entry focal_plane_x;
    focal_plane_x.key   = make_exif_tag_key(store.arena(), "exififd", 0xA20E);
    focal_plane_x.value = make_urational(0, 0);
    focal_plane_x.origin.block          = block;
    focal_plane_x.origin.order_in_block = 1;
    (void)store.add_entry(focal_plane_x);

    Entry focal_plane_y;
    focal_plane_y.key   = make_exif_tag_key(store.arena(), "exififd", 0xA20F);
    focal_plane_y.value = make_urational(0, 0);
    focal_plane_y.origin.block          = block;
    focal_plane_y.origin.order_in_block = 2;
    (void)store.add_entry(focal_plane_y);

    Entry gamma;
    gamma.key          = make_exif_tag_key(store.arena(), "exififd", 0xA500);
    gamma.value        = make_urational(0, 0);
    gamma.origin.block = block;
    gamma.origin.order_in_block = 3;
    (void)store.add_entry(gamma);

    Entry baseline_exposure;
    baseline_exposure.key   = make_exif_tag_key(store.arena(), "ifd0", 0xC62A);
    baseline_exposure.value = make_srational(0, 0);
    baseline_exposure.origin.block          = block;
    baseline_exposure.origin.order_in_block = 4;
    (void)store.add_entry(baseline_exposure);

    Entry model;
    model.key          = make_exif_tag_key(store.arena(), "ifd0", 0x0110);
    model.value        = make_text(store.arena(), "Portable Skip Test",
                                   TextEncoding::Ascii);
    model.origin.block = block;
    model.origin.order_in_block = 5;
    (void)store.add_entry(model);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("<exif:FlashEnergy>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:FocalPlaneXResolution>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:FocalPlaneYResolution>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:Gamma>"), std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:BaselineExposure>"), std::string_view::npos);
    EXPECT_NE(s.find("<tiff:Model>Portable Skip Test</tiff:Model>"),
              std::string_view::npos);
}


TEST(XmpDump, PortableNormalizesEnumAndGpsRefTextValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry compression;
    compression.key          = make_exif_tag_key(store.arena(), "ifd0", 0x0103);
    compression.value        = make_u16(4);
    compression.origin.block = block;
    compression.origin.order_in_block = 0;
    (void)store.add_entry(compression);

    Entry focal_plane_unit;
    focal_plane_unit.key = make_exif_tag_key(store.arena(), "exififd", 0xA210);
    focal_plane_unit.value                 = make_u16(5);
    focal_plane_unit.origin.block          = block;
    focal_plane_unit.origin.order_in_block = 1;
    (void)store.add_entry(focal_plane_unit);

    Entry color_space;
    color_space.key   = make_exif_tag_key(store.arena(), "exififd", 0xA001);
    color_space.value = make_u16(2);
    color_space.origin.block          = block;
    color_space.origin.order_in_block = 2;
    (void)store.add_entry(color_space);

    Entry file_source;
    file_source.key   = make_exif_tag_key(store.arena(), "exififd", 0xA300);
    file_source.value = make_u8(3);
    file_source.origin.block          = block;
    file_source.origin.order_in_block = 3;
    (void)store.add_entry(file_source);

    Entry gps_diff;
    gps_diff.key          = make_exif_tag_key(store.arena(), "gpsifd", 0x001E);
    gps_diff.value        = make_u16(0);
    gps_diff.origin.block = block;
    gps_diff.origin.order_in_block = 4;
    (void)store.add_entry(gps_diff);

    Entry gps_dest_dist_ref;
    gps_dest_dist_ref.key = make_exif_tag_key(store.arena(), "gpsifd", 0x0019);
    gps_dest_dist_ref.value                 = make_text(store.arena(), "K",
                                                        TextEncoding::Ascii);
    gps_dest_dist_ref.origin.block          = block;
    gps_dest_dist_ref.origin.order_in_block = 5;
    (void)store.add_entry(gps_dest_dist_ref);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<tiff:Compression>T6/Group 4 Fax</tiff:Compression>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<exif:FocalPlaneResolutionUnit>um</exif:FocalPlaneResolutionUnit>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<exif:ColorSpace>Adobe RGB</exif:ColorSpace>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:FileSource>Digital Camera</exif:FileSource>"),
              std::string_view::npos);
    EXPECT_NE(s.find(
                  "<exif:GPSDifferential>No Correction</exif:GPSDifferential>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<exif:GPSDestDistanceRef>Kilometers</exif:GPSDestDistanceRef>"),
        std::string_view::npos);
}

TEST(XmpDump, PortableSkipsGpsTimeStampWithoutGpsDateStamp)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::array<URational, 3> gps_time_triplet = {
        URational { 12, 1 },
        URational { 11, 1 },
        URational { 13, 1 },
    };
    Entry gps_time;
    gps_time.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0007);
    gps_time.value = make_urational_array(
        store.arena(), std::span<const URational>(gps_time_triplet.data(),
                                                  gps_time_triplet.size()));
    gps_time.origin.block          = block;
    gps_time.origin.order_in_block = 0;
    (void)store.add_entry(gps_time);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("<exif:GPSTimeStamp>"), std::string_view::npos);
}

TEST(XmpDump, PortableGpsTimeStampSupportsExifToolAlias)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry gps_date;
    gps_date.key          = make_exif_tag_key(store.arena(), "gpsifd", 0x001D);
    gps_date.value        = make_text(store.arena(), "2024:04:19",
                                      TextEncoding::Ascii);
    gps_date.origin.block = block;
    gps_date.origin.order_in_block = 0;
    (void)store.add_entry(gps_date);

    const std::array<URational, 3> gps_time_triplet = {
        URational { 12, 1 },
        URational { 11, 1 },
        URational { 13, 1 },
    };
    Entry gps_time;
    gps_time.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0007);
    gps_time.value = make_urational_array(
        store.arena(), std::span<const URational>(gps_time_triplet.data(),
                                                  gps_time_triplet.size()));
    gps_time.origin.block          = block;
    gps_time.origin.order_in_block = 1;
    (void)store.add_entry(gps_time);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif               = true;
    opts.include_existing_xmp       = false;
    opts.exiftool_gpsdatetime_alias = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find(
                  "<exif:GPSDateTime>2024-04-19T12:11:13Z</exif:GPSDateTime>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<exif:GPSTimeStamp>"), std::string_view::npos);
}

TEST(XmpDump, PortableMapsIptcToDcProperties)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string title = "Sunset";
    Entry object_name;
    object_name.key = make_iptc_dataset_key(2U, 5U);  // ObjectName
    object_name.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(title.data()),
                         title.size()));
    object_name.origin.block          = block;
    object_name.origin.order_in_block = 0;
    (void)store.add_entry(object_name);

    const std::string caption = "Golden hour over the lake";
    Entry caption_abstract;
    caption_abstract.key = make_iptc_dataset_key(2U, 120U);  // Caption-Abstract
    caption_abstract.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(caption.data()),
                         caption.size()));
    caption_abstract.origin.block          = block;
    caption_abstract.origin.order_in_block = 1;
    (void)store.add_entry(caption_abstract);

    const std::string rights = "Copyright 2026 OpenMeta";
    Entry copyright_notice;
    copyright_notice.key = make_iptc_dataset_key(2U, 116U);  // CopyrightNotice
    copyright_notice.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(rights.data()),
                         rights.size()));
    copyright_notice.origin.block          = block;
    copyright_notice.origin.order_in_block = 2;
    (void)store.add_entry(copyright_notice);

    const std::string creator_a = "Alice";
    Entry byline_a;
    byline_a.key = make_iptc_dataset_key(2U, 80U);  // By-line
    byline_a.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(creator_a.data()),
                         creator_a.size()));
    byline_a.origin.block          = block;
    byline_a.origin.order_in_block = 3;
    (void)store.add_entry(byline_a);

    const std::string creator_b = "Bob";
    Entry byline_b;
    byline_b.key = make_iptc_dataset_key(2U, 80U);  // By-line
    byline_b.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(creator_b.data()),
                         creator_b.size()));
    byline_b.origin.block          = block;
    byline_b.origin.order_in_block = 4;
    (void)store.add_entry(byline_b);

    const std::string keyword_a = "nature";
    Entry keyword_1;
    keyword_1.key = make_iptc_dataset_key(2U, 25U);  // Keywords
    keyword_1.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(keyword_a.data()),
                         keyword_a.size()));
    keyword_1.origin.block          = block;
    keyword_1.origin.order_in_block = 5;
    (void)store.add_entry(keyword_1);

    const std::string keyword_b = "sunset";
    Entry keyword_2;
    keyword_2.key = make_iptc_dataset_key(2U, 25U);  // Keywords
    keyword_2.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(keyword_b.data()),
                         keyword_b.size()));
    keyword_2.origin.block          = block;
    keyword_2.origin.order_in_block = 6;
    (void)store.add_entry(keyword_2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:title>"), std::string_view::npos);
    EXPECT_NE(s.find("<dc:description>"), std::string_view::npos);
    EXPECT_NE(s.find("<dc:rights>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Alt>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Sunset</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<rdf:li xml:lang=\"x-default\">Golden hour over the lake</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Copyright 2026 OpenMeta</rdf:li>"),
        std::string_view::npos);

    EXPECT_NE(s.find("<dc:creator>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Alice</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Bob</rdf:li>"), std::string_view::npos);

    EXPECT_NE(s.find("<dc:subject>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>nature</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>sunset</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortableComposesIptcCreationDateTimes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const auto add_iptc = [&](uint16_t dataset, std::string_view value,
                              uint32_t order) {
        Entry entry;
        entry.key = make_iptc_dataset_key(2U, dataset);
        entry.value
            = make_bytes(store.arena(),
                         std::span<const std::byte>(
                             reinterpret_cast<const std::byte*>(value.data()),
                             value.size()));
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        EXPECT_NE(store.add_entry(entry), kInvalidEntryId);
    };

    add_iptc(55U, "20240229", 0U);     // DateCreated
    add_iptc(60U, "123501+0900", 1U);  // TimeCreated
    add_iptc(62U, "20240830", 2U);     // DigitalCreationDate
    add_iptc(63U, "010203-0230", 3U);  // DigitalCreationTime
    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult result
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(result.status, XmpDumpStatus::Ok);

    const std::string_view packet(reinterpret_cast<const char*>(out.data()),
                                  static_cast<size_t>(result.written));
    EXPECT_NE(packet.find("<photoshop:DateCreated>2024-02-29T12:35:01+09:00"
                          "</photoshop:DateCreated>"),
              std::string_view::npos);
    EXPECT_NE(packet.find("<xmp:CreateDate>2024-08-30T01:02:03-02:30"
                          "</xmp:CreateDate>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableIptcCreationDatesHandlePartialAndMalformedFields)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const auto add_iptc = [&](uint16_t dataset, std::string_view value,
                              uint32_t order) {
        Entry entry;
        entry.key   = make_iptc_dataset_key(2U, dataset);
        entry.value = make_text(store.arena(), value, TextEncoding::Ascii);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        EXPECT_NE(store.add_entry(entry), kInvalidEntryId);
    };

    add_iptc(55U, "20240506", 0U);
    add_iptc(60U, "246000+0900", 1U);
    add_iptc(62U, "20230229", 2U);
    add_iptc(63U, "010203+0000", 3U);
    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult result
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(result.status, XmpDumpStatus::Ok);

    const std::string_view packet(reinterpret_cast<const char*>(out.data()),
                                  static_cast<size_t>(result.written));
    EXPECT_NE(packet.find(
                  "<photoshop:DateCreated>2024-05-06</photoshop:DateCreated>"),
              std::string_view::npos);
    EXPECT_EQ(packet.find("<photoshop:DateCreated>2024-05-06T"),
              std::string_view::npos);
    EXPECT_EQ(packet.find("<xmp:CreateDate>"), std::string_view::npos);
}

TEST(XmpDump, PortableIptcCreationDatesFollowConflictAndCanonicalizePolicies)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const auto add_iptc = [&](uint16_t dataset, std::string_view value,
                              uint32_t order) {
        Entry entry;
        entry.key   = make_iptc_dataset_key(2U, dataset);
        entry.value = make_text(store.arena(), value, TextEncoding::Ascii);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        EXPECT_NE(store.add_entry(entry), kInvalidEntryId);
    };
    add_iptc(55U, "20240829", 0U);
    add_iptc(60U, "123501+0900", 1U);
    add_iptc(62U, "20240830", 2U);
    add_iptc(63U, "010203-0230", 3U);

    Entry existing_date_created;
    existing_date_created.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/photoshop/1.0/", "DateCreated");
    existing_date_created.value        = make_text(store.arena(), "2001-02-03",
                                                   TextEncoding::Utf8);
    existing_date_created.origin.block = block;
    existing_date_created.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(existing_date_created), kInvalidEntryId);

    Entry existing_create_date;
    existing_create_date.key
        = make_xmp_property_key(store.arena(), "http://ns.adobe.com/xap/1.0/",
                                "CreateDate");
    existing_create_date.value = make_text(store.arena(), "2002-03-04T05:06:07",
                                           TextEncoding::Utf8);
    existing_create_date.origin.block          = block;
    existing_create_date.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(existing_create_date), kInvalidEntryId);
    store.finalize();

    const auto dump = [&](XmpConflictPolicy conflict,
                          XmpExistingStandardNamespacePolicy namespace_policy) {
        XmpPortableOptions opts;
        opts.include_exif                       = false;
        opts.include_iptc                       = true;
        opts.include_existing_xmp               = true;
        opts.conflict_policy                    = conflict;
        opts.existing_standard_namespace_policy = namespace_policy;

        std::vector<std::byte> out(4096);
        const XmpDumpResult result = dump_xmp_portable(
            store, std::span<std::byte>(out.data(), out.size()), opts);
        EXPECT_EQ(result.status, XmpDumpStatus::Ok);
        return std::string(reinterpret_cast<const char*>(out.data()),
                           static_cast<size_t>(result.written));
    };

    const std::string existing
        = dump(XmpConflictPolicy::ExistingWins,
               XmpExistingStandardNamespacePolicy::PreserveAll);
    EXPECT_NE(existing.find(
                  "<photoshop:DateCreated>2001-02-03</photoshop:DateCreated>"),
              std::string::npos);
    EXPECT_NE(existing.find(
                  "<xmp:CreateDate>2002-03-04T05:06:07</xmp:CreateDate>"),
              std::string::npos);

    const std::string generated
        = dump(XmpConflictPolicy::GeneratedWins,
               XmpExistingStandardNamespacePolicy::PreserveAll);
    EXPECT_NE(generated.find("<photoshop:DateCreated>2024-08-29T12:35:01+09:00"
                             "</photoshop:DateCreated>"),
              std::string::npos);
    EXPECT_NE(generated.find("<xmp:CreateDate>2024-08-30T01:02:03-02:30"
                             "</xmp:CreateDate>"),
              std::string::npos);

    const std::string canonical
        = dump(XmpConflictPolicy::ExistingWins,
               XmpExistingStandardNamespacePolicy::CanonicalizeManaged);
    EXPECT_NE(canonical.find("<photoshop:DateCreated>2024-08-29T12:35:01+09:00"
                             "</photoshop:DateCreated>"),
              std::string::npos);
    EXPECT_NE(canonical.find("<xmp:CreateDate>2024-08-30T01:02:03-02:30"
                             "</xmp:CreateDate>"),
              std::string::npos);
    EXPECT_EQ(canonical.find("2001-02-03"), std::string::npos);
    EXPECT_EQ(canonical.find("2002-03-04T05:06:07"), std::string::npos);
}

TEST(XmpDump, PortableIptcDoesNotOverrideExistingXmpWhenIncluded)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string caption = "From IPTC";
    Entry caption_abstract;
    caption_abstract.key = make_iptc_dataset_key(2U, 120U);  // Caption-Abstract
    caption_abstract.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(caption.data()),
                         caption.size()));
    caption_abstract.origin.block          = block;
    caption_abstract.origin.order_in_block = 0;
    (void)store.add_entry(caption_abstract);

    Entry xmp_description;
    xmp_description.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "description");
    xmp_description.value                 = make_text(store.arena(), "From XMP",
                                                      TextEncoding::Utf8);
    xmp_description.origin.block          = block;
    xmp_description.origin.order_in_block = 1;
    (void)store.add_entry(xmp_description);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:description>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">From XMP</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:description>From IPTC</dc:description>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableExistingXmpCanOverrideExifProjection)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry exif_make;
    exif_make.key = make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
    exif_make.value
        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    exif_make.origin.block          = block;
    exif_make.origin.order_in_block = 0;
    (void)store.add_entry(exif_make);

    Entry xmp_make;
    xmp_make.key = make_xmp_property_key(store.arena(),
                                         "http://ns.adobe.com/tiff/1.0/",
                                         "Make");
    xmp_make.value = make_text(store.arena(), "Nikon", TextEncoding::Utf8);
    xmp_make.origin.block          = block;
    xmp_make.origin.order_in_block = 1;
    (void)store.add_entry(xmp_make);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<tiff:Make>Nikon</tiff:Make>"), std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:Make>Canon</tiff:Make>"), std::string_view::npos);
}

TEST(XmpDump,
     PortableCanonicalizesManagedStandardNamespacesOnlyWhenReplacementExists)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry exif_make;
    exif_make.key = make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
    exif_make.value
        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    exif_make.origin.block          = block;
    exif_make.origin.order_in_block = 0;
    (void)store.add_entry(exif_make);

    Entry xmp_make;
    xmp_make.key = make_xmp_property_key(store.arena(),
                                         "http://ns.adobe.com/tiff/1.0/",
                                         "Make");
    xmp_make.value = make_text(store.arena(), "Nikon", TextEncoding::Utf8);
    xmp_make.origin.block          = block;
    xmp_make.origin.order_in_block = 1;
    (void)store.add_entry(xmp_make);

    Entry xmp_model;
    xmp_model.key = make_xmp_property_key(store.arena(),
                                          "http://ns.adobe.com/tiff/1.0/",
                                          "Model");
    xmp_model.value = make_text(store.arena(), "EOS R6",
                                TextEncoding::Utf8);
    xmp_model.origin.block          = block;
    xmp_model.origin.order_in_block = 2;
    (void)store.add_entry(xmp_model);

    Entry xmp_description;
    xmp_description.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "description");
    xmp_description.value = make_text(store.arena(), "From XMP",
                                      TextEncoding::Utf8);
    xmp_description.origin.block          = block;
    xmp_description.origin.order_in_block = 3;
    (void)store.add_entry(xmp_description);

    Entry xmp_subject;
    xmp_subject.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "subject[1]");
    xmp_subject.value = make_text(store.arena(), "xmp-keyword",
                                  TextEncoding::Utf8);
    xmp_subject.origin.block          = block;
    xmp_subject.origin.order_in_block = 4;
    (void)store.add_entry(xmp_subject);

    Entry xmp_creator_tool;
    xmp_creator_tool.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp_creator_tool.value = make_text(store.arena(), "Tool",
                                       TextEncoding::Utf8);
    xmp_creator_tool.origin.block          = block;
    xmp_creator_tool.origin.order_in_block = 5;
    (void)store.add_entry(xmp_creator_tool);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<tiff:Make>Canon</tiff:Make>"), std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:Make>Nikon</tiff:Make>"), std::string_view::npos);
    EXPECT_NE(s.find("<tiff:Model>EOS R6</tiff:Model>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<dc:description>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">From XMP</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<dc:subject>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>xmp-keyword</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmp:CreatorTool>Tool</xmp:CreatorTool>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableGeneratedIptcCanOverrideExistingXmp)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string caption = "From IPTC";
    Entry caption_abstract;
    caption_abstract.key = make_iptc_dataset_key(2U, 120U);
    caption_abstract.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(caption.data()),
                         caption.size()));
    caption_abstract.origin.block          = block;
    caption_abstract.origin.order_in_block = 0;
    (void)store.add_entry(caption_abstract);

    Entry xmp_description;
    xmp_description.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "description");
    xmp_description.value = make_text(store.arena(), "From XMP",
                                      TextEncoding::Utf8);
    xmp_description.origin.block          = block;
    xmp_description.origin.order_in_block = 1;
    (void)store.add_entry(xmp_description);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::GeneratedWins;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:description>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">From IPTC</rdf:li>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("<dc:description>From XMP</dc:description>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableGeneratedIptcCanOverrideExistingIndexedXmp)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string keyword = "iptc-keyword";
    Entry iptc_keyword;
    iptc_keyword.key = make_iptc_dataset_key(2U, 25U);
    iptc_keyword.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(
                         reinterpret_cast<const std::byte*>(keyword.data()),
                         keyword.size()));
    iptc_keyword.origin.block          = block;
    iptc_keyword.origin.order_in_block = 0;
    (void)store.add_entry(iptc_keyword);

    Entry xmp_subject;
    xmp_subject.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "subject[1]");
    xmp_subject.value = make_text(store.arena(), "xmp-keyword",
                                  TextEncoding::Utf8);
    xmp_subject.origin.block          = block;
    xmp_subject.origin.order_in_block = 1;
    (void)store.add_entry(xmp_subject);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::GeneratedWins;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:subject>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>iptc-keyword</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li>xmp-keyword</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump,
     PortableExistingXmpCanonicalizesSubjectScalarToExplicitIndexedShape)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry xmp_subject_scalar;
    xmp_subject_scalar.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "subject");
    xmp_subject_scalar.value = make_text(store.arena(), "scalar-keyword",
                                         TextEncoding::Utf8);
    xmp_subject_scalar.origin.block          = block;
    xmp_subject_scalar.origin.order_in_block = 0;
    (void)store.add_entry(xmp_subject_scalar);

    Entry xmp_subject_indexed;
    xmp_subject_indexed.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "subject[1]");
    xmp_subject_indexed.value = make_text(store.arena(), "indexed-keyword",
                                          TextEncoding::Utf8);
    xmp_subject_indexed.origin.block          = block;
    xmp_subject_indexed.origin.order_in_block = 1;
    (void)store.add_entry(xmp_subject_indexed);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:subject>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>indexed-keyword</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:subject>scalar-keyword</dc:subject>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li>scalar-keyword</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump,
     PortableCanonicalizeManagedReplacesXDefaultAltTextAndPreservesOtherLocales)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string title = "Generated Title";
    Entry iptc_title;
    iptc_title.key = make_iptc_dataset_key(2U, 5U);
    iptc_title.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(title.data()), title.size()));
    iptc_title.origin.block          = block;
    iptc_title.origin.order_in_block = 0;
    (void)store.add_entry(iptc_title);

    Entry xmp_title_default;
    xmp_title_default.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "title[@xml:lang=x-default]");
    xmp_title_default.value = make_text(store.arena(), "Default title",
                                        TextEncoding::Utf8);
    xmp_title_default.origin.block          = block;
    xmp_title_default.origin.order_in_block = 1;
    (void)store.add_entry(xmp_title_default);

    Entry xmp_title_fr;
    xmp_title_fr.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "title[@xml:lang=fr-FR]");
    xmp_title_fr.value = make_text(store.arena(), "Titre localise",
                                   TextEncoding::Utf8);
    xmp_title_fr.origin.block          = block;
    xmp_title_fr.origin.order_in_block = 2;
    (void)store.add_entry(xmp_title_fr);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<rdf:Alt>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Generated Title</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"fr-FR\">Titre localise</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li xml:lang=\"x-default\">Default title</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump,
     PortableCanonicalizeManagedDropsStructuredDescendantsForGeneratedManagedBases)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string title = "Generated Title";
    Entry iptc_title;
    iptc_title.key = make_iptc_dataset_key(2U, 5U);
    iptc_title.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(title.data()), title.size()));
    iptc_title.origin.block          = block;
    iptc_title.origin.order_in_block = 0;
    (void)store.add_entry(iptc_title);

    const std::string keyword = "museum";
    Entry iptc_keyword;
    iptc_keyword.key = make_iptc_dataset_key(2U, 25U);
    iptc_keyword.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       keyword.data()),
                                   keyword.size()));
    iptc_keyword.origin.block          = block;
    iptc_keyword.origin.order_in_block = 1;
    (void)store.add_entry(iptc_keyword);

    const std::string location = "Louvre";
    Entry iptc_location;
    iptc_location.key = make_iptc_dataset_key(2U, 92U);
    iptc_location.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       location.data()),
                                   location.size()));
    iptc_location.origin.block          = block;
    iptc_location.origin.order_in_block = 2;
    (void)store.add_entry(iptc_location);

    Entry xmp_title_structured;
    xmp_title_structured.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "title/LegacyStructured");
    xmp_title_structured.value = make_text(store.arena(), "Legacy title",
                                           TextEncoding::Utf8);
    xmp_title_structured.origin.block          = block;
    xmp_title_structured.origin.order_in_block = 3;
    (void)store.add_entry(xmp_title_structured);

    Entry xmp_subject_structured;
    xmp_subject_structured.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "subject/LegacyStructured");
    xmp_subject_structured.value = make_text(store.arena(), "Legacy subject",
                                             TextEncoding::Utf8);
    xmp_subject_structured.origin.block          = block;
    xmp_subject_structured.origin.order_in_block = 4;
    (void)store.add_entry(xmp_subject_structured);

    Entry xmp_location_structured;
    xmp_location_structured.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "Location/LegacyStructured");
    xmp_location_structured.value = make_text(store.arena(), "Legacy location",
                                              TextEncoding::Utf8);
    xmp_location_structured.origin.block          = block;
    xmp_location_structured.origin.order_in_block = 5;
    (void)store.add_entry(xmp_location_structured);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Generated Title</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>museum</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:Location>Louvre</Iptc4xmpCore:Location>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:LegacyStructured>"), std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpCore:LegacyStructured>"),
              std::string_view::npos);
}

TEST(XmpDump,
     PortableCanonicalizeManagedDropsFlatCrossShapeExistingManagedProperties)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string keyword = "iptc-keyword";
    Entry iptc_keyword;
    iptc_keyword.key = make_iptc_dataset_key(2U, 25U);
    iptc_keyword.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       keyword.data()),
                                   keyword.size()));
    iptc_keyword.origin.block          = block;
    iptc_keyword.origin.order_in_block = 0;
    (void)store.add_entry(iptc_keyword);

    const std::string location = "Louvre";
    Entry iptc_location;
    iptc_location.key = make_iptc_dataset_key(2U, 92U);
    iptc_location.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       location.data()),
                                   location.size()));
    iptc_location.origin.block          = block;
    iptc_location.origin.order_in_block = 1;
    (void)store.add_entry(iptc_location);

    Entry xmp_subject_scalar;
    xmp_subject_scalar.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "subject");
    xmp_subject_scalar.value = make_text(store.arena(), "Legacy subject",
                                         TextEncoding::Utf8);
    xmp_subject_scalar.origin.block          = block;
    xmp_subject_scalar.origin.order_in_block = 2;
    (void)store.add_entry(xmp_subject_scalar);

    Entry xmp_location_indexed;
    xmp_location_indexed.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "Location[1]");
    xmp_location_indexed.value = make_text(store.arena(),
                                           "Legacy indexed location",
                                           TextEncoding::Utf8);
    xmp_location_indexed.origin.block          = block;
    xmp_location_indexed.origin.order_in_block = 3;
    (void)store.add_entry(xmp_location_indexed);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<rdf:li>iptc-keyword</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:Location>Louvre</Iptc4xmpCore:Location>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:subject>Legacy subject</dc:subject>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("Legacy indexed location"), std::string_view::npos);
}

TEST(XmpDump, PortableDropsCustomExistingNamespacesByDefault)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry xmp_flag;
    xmp_flag.key = make_xmp_property_key(store.arena(),
                                         "urn:vendor:test:1.0/", "Flag");
    xmp_flag.value = make_text(store.arena(), "Alpha", TextEncoding::Utf8);
    xmp_flag.origin.block          = block;
    xmp_flag.origin.order_in_block = 0;
    (void)store.add_entry(xmp_flag);
    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("urn:vendor:test:1.0/"), std::string_view::npos);
    EXPECT_EQ(s.find("<omns1:Flag>Alpha</omns1:Flag>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableCanPreserveCustomExistingNamespaces)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry xmp_flag;
    xmp_flag.key = make_xmp_property_key(store.arena(),
                                         "urn:vendor:test:1.0/", "Flag");
    xmp_flag.value = make_text(store.arena(), "Alpha", TextEncoding::Utf8);
    xmp_flag.origin.block          = block;
    xmp_flag.origin.order_in_block = 0;
    (void)store.add_entry(xmp_flag);
    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;
    opts.existing_namespace_policy
        = XmpExistingNamespacePolicy::PreserveCustom;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("xmlns:omns1=\"urn:vendor:test:1.0/\""),
              std::string_view::npos);
    EXPECT_NE(s.find("<omns1:Flag>Alpha</omns1:Flag>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableCustomNamespaceDoesNotHijackStandardPrefix)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry shadow_flag;
    shadow_flag.key = make_xmp_property_key(store.arena(),
                                            "urn:vendor:shadow-xmp:", "Flag");
    shadow_flag.value = make_text(store.arena(), "shadow", TextEncoding::Utf8);
    shadow_flag.origin.block          = block;
    shadow_flag.origin.order_in_block = 0;
    (void)store.add_entry(shadow_flag);

    Entry creator_tool;
    creator_tool.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    creator_tool.value = make_text(store.arena(), "OpenMeta",
                                   TextEncoding::Utf8);
    creator_tool.origin.block          = block;
    creator_tool.origin.order_in_block = 1;
    (void)store.add_entry(creator_tool);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;
    opts.existing_namespace_policy
        = XmpExistingNamespacePolicy::PreserveCustom;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("xmlns:omns1=\"urn:vendor:shadow-xmp:\""),
              std::string_view::npos);
    EXPECT_NE(s.find("<omns1:Flag>shadow</omns1:Flag>"),
              std::string_view::npos);
    EXPECT_NE(s.find("xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\""),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmp:CreatorTool>OpenMeta</xmp:CreatorTool>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("xmlns:xmp=\"urn:vendor:shadow-xmp:\""),
              std::string_view::npos);
}

TEST(XmpDump, PortablePreservesXmpRightsStandardNamespace)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry marked;
    marked.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/", "Marked");
    marked.value = make_text(store.arena(), "True", TextEncoding::Utf8);
    marked.origin.block          = block;
    marked.origin.order_in_block = 0;
    (void)store.add_entry(marked);

    Entry usage_terms;
    usage_terms.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/",
        "UsageTerms[@xml:lang=x-default]");
    usage_terms.value = make_text(store.arena(), "Licensed use only",
                                  TextEncoding::Utf8);
    usage_terms.origin.block          = block;
    usage_terms.origin.order_in_block = 1;
    (void)store.add_entry(usage_terms);

    Entry web_statement;
    web_statement.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/",
        "WebStatement");
    web_statement.value = make_text(store.arena(),
                                    "https://example.test/license",
                                    TextEncoding::Utf8);
    web_statement.origin.block          = block;
    web_statement.origin.order_in_block = 2;
    (void)store.add_entry(web_statement);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("xmlns:xmpRights=\"http://ns.adobe.com/xap/1.0/rights/\""),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpRights:Marked>True</xmpRights:Marked>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpRights:UsageTerms>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Licensed use only</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpRights:WebStatement>https://example.test/license</xmpRights:WebStatement>"),
        std::string_view::npos);
}

TEST(XmpDump, PortablePreservesXmpMmStandardNamespace)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry document_id;
    document_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "DocumentID");
    document_id.value = make_text(store.arena(), "xmp.did:1234",
                                  TextEncoding::Utf8);
    document_id.origin.block          = block;
    document_id.origin.order_in_block = 0;
    (void)store.add_entry(document_id);

    Entry instance_id;
    instance_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "InstanceID");
    instance_id.value = make_text(store.arena(), "xmp.iid:5678",
                                  TextEncoding::Utf8);
    instance_id.origin.block          = block;
    instance_id.origin.order_in_block = 1;
    (void)store.add_entry(instance_id);

    Entry original_document_id;
    original_document_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "OriginalDocumentID");
    original_document_id.value = make_text(store.arena(), "xmp.did:0001",
                                           TextEncoding::Utf8);
    original_document_id.origin.block          = block;
    original_document_id.origin.order_in_block = 2;
    (void)store.add_entry(original_document_id);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\""),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpMM:DocumentID>xmp.did:1234</xmpMM:DocumentID>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpMM:InstanceID>xmp.iid:5678</xmpMM:InstanceID>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpMM:OriginalDocumentID>xmp.did:0001</xmpMM:OriginalDocumentID>"),
        std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesXmpMmStructuredBaseShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry flat_derived_from;
    flat_derived_from.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "DerivedFrom");
    flat_derived_from.value = make_text(store.arena(), "legacy-flat",
                                        TextEncoding::Utf8);
    flat_derived_from.origin.block          = block;
    flat_derived_from.origin.order_in_block = 0;
    (void)store.add_entry(flat_derived_from);

    Entry derived_document_id;
    derived_document_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "DerivedFrom/stRef:documentID");
    derived_document_id.value = make_text(store.arena(), "xmp.did:base",
                                          TextEncoding::Utf8);
    derived_document_id.origin.block          = block;
    derived_document_id.origin.order_in_block = 1;
    (void)store.add_entry(derived_document_id);

    Entry derived_instance_id;
    derived_instance_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "DerivedFrom/stRef:instanceID");
    derived_instance_id.value = make_text(store.arena(), "xmp.iid:base",
                                          TextEncoding::Utf8);
    derived_instance_id.origin.block          = block;
    derived_instance_id.origin.order_in_block = 2;
    (void)store.add_entry(derived_instance_id);

    Entry flat_managed_from;
    flat_managed_from.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "ManagedFrom");
    flat_managed_from.value = make_text(store.arena(), "legacy-managed",
                                        TextEncoding::Utf8);
    flat_managed_from.origin.block          = block;
    flat_managed_from.origin.order_in_block = 3;
    (void)store.add_entry(flat_managed_from);

    Entry managed_document_id;
    managed_document_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "ManagedFrom/stRef:documentID");
    managed_document_id.value = make_text(store.arena(), "xmp.did:managed",
                                          TextEncoding::Utf8);
    managed_document_id.origin.block          = block;
    managed_document_id.origin.order_in_block = 4;
    (void)store.add_entry(managed_document_id);

    Entry managed_instance_id;
    managed_instance_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "ManagedFrom/stRef:instanceID");
    managed_instance_id.value = make_text(store.arena(), "xmp.iid:managed",
                                          TextEncoding::Utf8);
    managed_instance_id.origin.block          = block;
    managed_instance_id.origin.order_in_block = 5;
    (void)store.add_entry(managed_instance_id);

    Entry flat_ingredients;
    flat_ingredients.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "Ingredients");
    flat_ingredients.value = make_text(store.arena(), "legacy-ingredients",
                                       TextEncoding::Utf8);
    flat_ingredients.origin.block          = block;
    flat_ingredients.origin.order_in_block = 6;
    (void)store.add_entry(flat_ingredients);

    Entry ingredients_document_id;
    ingredients_document_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Ingredients[1]/stRef:documentID");
    ingredients_document_id.value = make_text(
        store.arena(), "xmp.did:ingredient", TextEncoding::Utf8);
    ingredients_document_id.origin.block          = block;
    ingredients_document_id.origin.order_in_block = 7;
    (void)store.add_entry(ingredients_document_id);

    Entry ingredients_instance_id;
    ingredients_instance_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Ingredients[1]/stRef:instanceID");
    ingredients_instance_id.value = make_text(
        store.arena(), "xmp.iid:ingredient", TextEncoding::Utf8);
    ingredients_instance_id.origin.block          = block;
    ingredients_instance_id.origin.order_in_block = 8;
    (void)store.add_entry(ingredients_instance_id);

    Entry flat_rendition_of;
    flat_rendition_of.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "RenditionOf");
    flat_rendition_of.value = make_text(store.arena(), "legacy-rendition",
                                        TextEncoding::Utf8);
    flat_rendition_of.origin.block          = block;
    flat_rendition_of.origin.order_in_block = 9;
    (void)store.add_entry(flat_rendition_of);

    Entry rendition_document_id;
    rendition_document_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "RenditionOf/stRef:documentID");
    rendition_document_id.value = make_text(
        store.arena(), "xmp.did:rendition", TextEncoding::Utf8);
    rendition_document_id.origin.block          = block;
    rendition_document_id.origin.order_in_block = 10;
    (void)store.add_entry(rendition_document_id);

    Entry rendition_file_path;
    rendition_file_path.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "RenditionOf/stRef:filePath");
    rendition_file_path.value = make_text(
        store.arena(), "/tmp/rendition.jpg", TextEncoding::Utf8);
    rendition_file_path.origin.block          = block;
    rendition_file_path.origin.order_in_block = 11;
    (void)store.add_entry(rendition_file_path);

    Entry rendition_class;
    rendition_class.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "RenditionOf/stRef:renditionClass");
    rendition_class.value = make_text(store.arena(), "proof:pdf",
                                      TextEncoding::Utf8);
    rendition_class.origin.block          = block;
    rendition_class.origin.order_in_block = 12;
    (void)store.add_entry(rendition_class);

    Entry flat_manifest;
    flat_manifest.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "Manifest");
    flat_manifest.value = make_text(store.arena(), "legacy-manifest",
                                    TextEncoding::Utf8);
    flat_manifest.origin.block          = block;
    flat_manifest.origin.order_in_block = 13;
    (void)store.add_entry(flat_manifest);

    Entry manifest_link_form;
    manifest_link_form.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Manifest[1]/stMfs:linkForm");
    manifest_link_form.value = make_text(
        store.arena(), "EmbedByReference", TextEncoding::Utf8);
    manifest_link_form.origin.block          = block;
    manifest_link_form.origin.order_in_block = 14;
    (void)store.add_entry(manifest_link_form);

    Entry manifest_file_path;
    manifest_file_path.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Manifest[1]/stMfs:reference/stRef:filePath");
    manifest_file_path.value = make_text(
        store.arena(), "C:\\some path\\file.ext", TextEncoding::Utf8);
    manifest_file_path.origin.block          = block;
    manifest_file_path.origin.order_in_block = 15;
    (void)store.add_entry(manifest_file_path);

    Entry flat_history;
    flat_history.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "History");
    flat_history.value = make_text(store.arena(), "legacy-history",
                                   TextEncoding::Utf8);
    flat_history.origin.block          = block;
    flat_history.origin.order_in_block = 16;
    (void)store.add_entry(flat_history);

    Entry history_action;
    history_action.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "History[1]/stEvt:action");
    history_action.value = make_text(store.arena(), "saved",
                                     TextEncoding::Utf8);
    history_action.origin.block          = block;
    history_action.origin.order_in_block = 17;
    (void)store.add_entry(history_action);

    Entry history_when;
    history_when.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "History[1]/stEvt:when");
    history_when.value = make_text(store.arena(), "2026-04-15T09:00:00Z",
                                   TextEncoding::Utf8);
    history_when.origin.block          = block;
    history_when.origin.order_in_block = 18;
    (void)store.add_entry(history_when);

    Entry flat_versions;
    flat_versions.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "Versions");
    flat_versions.value = make_text(store.arena(), "legacy-versions",
                                    TextEncoding::Utf8);
    flat_versions.origin.block          = block;
    flat_versions.origin.order_in_block = 19;
    (void)store.add_entry(flat_versions);

    Entry flat_versions_event;
    flat_versions_event.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/stVer:event");
    flat_versions_event.value = make_text(
        store.arena(), "legacy-event", TextEncoding::Utf8);
    flat_versions_event.origin.block          = block;
    flat_versions_event.origin.order_in_block = 20;
    (void)store.add_entry(flat_versions_event);

    Entry versions_version;
    versions_version.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/stVer:version");
    versions_version.value = make_text(store.arena(), "1.0",
                                       TextEncoding::Utf8);
    versions_version.origin.block          = block;
    versions_version.origin.order_in_block = 21;
    (void)store.add_entry(versions_version);

    Entry versions_comments;
    versions_comments.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/stVer:comments");
    versions_comments.value = make_text(store.arena(), "Initial import",
                                        TextEncoding::Utf8);
    versions_comments.origin.block          = block;
    versions_comments.origin.order_in_block = 22;
    (void)store.add_entry(versions_comments);

    Entry versions_modifier;
    versions_modifier.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/stVer:modifier");
    versions_modifier.value = make_text(store.arena(), "OpenMeta",
                                        TextEncoding::Utf8);
    versions_modifier.origin.block          = block;
    versions_modifier.origin.order_in_block = 23;
    (void)store.add_entry(versions_modifier);

    Entry versions_modify_date;
    versions_modify_date.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/stVer:modifyDate");
    versions_modify_date.value = make_text(
        store.arena(), "2026-04-16T10:15:00Z", TextEncoding::Utf8);
    versions_modify_date.origin.block          = block;
    versions_modify_date.origin.order_in_block = 24;
    (void)store.add_entry(versions_modify_date);

    Entry versions_event_action;
    versions_event_action.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/stVer:event/stEvt:action");
    versions_event_action.value = make_text(store.arena(), "saved",
                                            TextEncoding::Utf8);
    versions_event_action.origin.block          = block;
    versions_event_action.origin.order_in_block = 25;
    (void)store.add_entry(versions_event_action);

    Entry versions_event_changed;
    versions_event_changed.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/stVer:event/stEvt:changed");
    versions_event_changed.value = make_text(store.arena(), "/metadata",
                                             TextEncoding::Utf8);
    versions_event_changed.origin.block          = block;
    versions_event_changed.origin.order_in_block = 26;
    (void)store.add_entry(versions_event_changed);

    Entry versions_event_when;
    versions_event_when.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/stVer:event/stEvt:when");
    versions_event_when.value = make_text(
        store.arena(), "2026-04-16T10:15:00Z", TextEncoding::Utf8);
    versions_event_when.origin.block          = block;
    versions_event_when.origin.order_in_block = 27;
    (void)store.add_entry(versions_event_when);

    Entry flat_pantry;
    flat_pantry.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "Pantry");
    flat_pantry.value = make_text(store.arena(), "legacy-pantry",
                                  TextEncoding::Utf8);
    flat_pantry.origin.block          = block;
    flat_pantry.origin.order_in_block = 28;
    (void)store.add_entry(flat_pantry);

    Entry pantry_instance_id;
    pantry_instance_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Pantry[1]/InstanceID");
    pantry_instance_id.value = make_text(store.arena(), "uuid:pantry-1",
                                         TextEncoding::Utf8);
    pantry_instance_id.origin.block          = block;
    pantry_instance_id.origin.order_in_block = 29;
    (void)store.add_entry(pantry_instance_id);

    Entry pantry_format;
    pantry_format.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Pantry[1]/dc:format");
    pantry_format.value = make_text(store.arena(), "image/jpeg",
                                    TextEncoding::Utf8);
    pantry_format.origin.block          = block;
    pantry_format.origin.order_in_block = 30;
    (void)store.add_entry(pantry_format);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("xmlns:stRef=\"http://ns.adobe.com/xap/1.0/sType/ResourceRef#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stEvt=\"http://ns.adobe.com/xap/1.0/sType/ResourceEvent#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpMM:DerivedFrom rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<stRef:documentID>xmp.did:base</stRef:documentID>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<stRef:instanceID>xmp.iid:base</stRef:instanceID>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpMM:ManagedFrom rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:documentID>xmp.did:managed</stRef:documentID>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:instanceID>xmp.iid:managed</stRef:instanceID>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpMM:Ingredients>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:documentID>xmp.did:ingredient</stRef:documentID>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:instanceID>xmp.iid:ingredient</stRef:instanceID>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpMM:RenditionOf rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:documentID>xmp.did:rendition</stRef:documentID>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:filePath>/tmp/rendition.jpg</stRef:filePath>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:renditionClass>proof:pdf</stRef:renditionClass>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stMfs=\"http://ns.adobe.com/xap/1.0/sType/ManifestItem#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stVer=\"http://ns.adobe.com/xap/1.0/sType/Version#\""),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpMM:Manifest>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<stMfs:linkForm>EmbedByReference</stMfs:linkForm>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stMfs:reference rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:filePath>C:\\some path\\file.ext</stRef:filePath>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpMM:History>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<stEvt:action>saved</stEvt:action>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<stEvt:when>2026-04-15T09:00:00Z</stEvt:when>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpMM:Versions>"), std::string_view::npos);
    EXPECT_NE(s.find("<stVer:version>1.0</stVer:version>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<stVer:comments>Initial import</stVer:comments>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<stVer:modifier>OpenMeta</stVer:modifier>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<stVer:modifyDate>2026-04-16T10:15:00Z</stVer:modifyDate>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stVer:event rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<stEvt:changed>/metadata</stEvt:changed>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<stEvt:when>2026-04-16T10:15:00Z</stEvt:when>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpMM:Pantry>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpMM:InstanceID>uuid:pantry-1</xmpMM:InstanceID>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<dc:format>image/jpeg</dc:format>"),
              std::string_view::npos);

    EXPECT_EQ(s.find("<xmpMM:DerivedFrom>legacy-flat</xmpMM:DerivedFrom>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:ManagedFrom>legacy-managed</xmpMM:ManagedFrom>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:Ingredients>legacy-ingredients</xmpMM:Ingredients>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:RenditionOf>legacy-rendition</xmpMM:RenditionOf>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:Manifest>legacy-manifest</xmpMM:Manifest>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:History>legacy-history</xmpMM:History>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:Versions>legacy-versions</xmpMM:Versions>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:Pantry>legacy-pantry</xmpMM:Pantry>"),
              std::string_view::npos);
    EXPECT_EQ(
        s.find("<stVer:event>legacy-event</stVer:event>"),
        std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesAdobeStructuredWorkflowNamespaces)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry flat_job_ref;
    flat_job_ref.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/bj/", "JobRef");
    flat_job_ref.value = make_text(store.arena(), "legacy-job-ref",
                                   TextEncoding::Utf8);
    flat_job_ref.origin.block          = block;
    flat_job_ref.origin.order_in_block = 0;
    (void)store.add_entry(flat_job_ref);

    Entry job_ref_id;
    job_ref_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/bj/",
        "JobRef[1]/stJob:id");
    job_ref_id.value = make_text(store.arena(), "job-1",
                                 TextEncoding::Utf8);
    job_ref_id.origin.block          = block;
    job_ref_id.origin.order_in_block = 1;
    (void)store.add_entry(job_ref_id);

    Entry job_ref_name;
    job_ref_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/bj/",
        "JobRef[1]/stJob:name");
    job_ref_name.value = make_text(store.arena(), "Layout Pass",
                                   TextEncoding::Utf8);
    job_ref_name.origin.block          = block;
    job_ref_name.origin.order_in_block = 2;
    (void)store.add_entry(job_ref_name);

    Entry job_ref_url;
    job_ref_url.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/bj/",
        "JobRef[1]/stJob:url");
    job_ref_url.value = make_text(store.arena(),
                                  "https://example.test/job/1",
                                  TextEncoding::Utf8);
    job_ref_url.origin.block          = block;
    job_ref_url.origin.order_in_block = 3;
    (void)store.add_entry(job_ref_url);

    Entry flat_max_page_size;
    flat_max_page_size.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "MaxPageSize");
    flat_max_page_size.value = make_text(store.arena(), "legacy-page-size",
                                         TextEncoding::Utf8);
    flat_max_page_size.origin.block          = block;
    flat_max_page_size.origin.order_in_block = 4;
    (void)store.add_entry(flat_max_page_size);

    Entry max_page_size_w;
    max_page_size_w.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "MaxPageSize/stDim:w");
    max_page_size_w.value = make_text(store.arena(), "8.5",
                                      TextEncoding::Utf8);
    max_page_size_w.origin.block          = block;
    max_page_size_w.origin.order_in_block = 5;
    (void)store.add_entry(max_page_size_w);

    Entry max_page_size_h;
    max_page_size_h.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "MaxPageSize/stDim:h");
    max_page_size_h.value = make_text(store.arena(), "11",
                                      TextEncoding::Utf8);
    max_page_size_h.origin.block          = block;
    max_page_size_h.origin.order_in_block = 6;
    (void)store.add_entry(max_page_size_h);

    Entry max_page_size_unit;
    max_page_size_unit.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "MaxPageSize/stDim:unit");
    max_page_size_unit.value = make_text(store.arena(), "inch",
                                         TextEncoding::Utf8);
    max_page_size_unit.origin.block          = block;
    max_page_size_unit.origin.order_in_block = 7;
    (void)store.add_entry(max_page_size_unit);

    Entry flat_fonts;
    flat_fonts.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/", "Fonts");
    flat_fonts.value = make_text(store.arena(), "legacy-fonts",
                                 TextEncoding::Utf8);
    flat_fonts.origin.block          = block;
    flat_fonts.origin.order_in_block = 8;
    (void)store.add_entry(flat_fonts);

    Entry font_name;
    font_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "Fonts[1]/stFnt:fontName");
    font_name.value = make_text(store.arena(), "Source Serif",
                                TextEncoding::Utf8);
    font_name.origin.block          = block;
    font_name.origin.order_in_block = 9;
    (void)store.add_entry(font_name);

    Entry font_child_file;
    font_child_file.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "Fonts[1]/stFnt:childFontFiles");
    font_child_file.value = make_text(store.arena(),
                                      "SourceSerif-Regular.otf",
                                      TextEncoding::Utf8);
    font_child_file.origin.block          = block;
    font_child_file.origin.order_in_block = 10;
    (void)store.add_entry(font_child_file);

    Entry flat_colorants;
    flat_colorants.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "Colorants");
    flat_colorants.value = make_text(store.arena(), "legacy-colorants",
                                     TextEncoding::Utf8);
    flat_colorants.origin.block          = block;
    flat_colorants.origin.order_in_block = 11;
    (void)store.add_entry(flat_colorants);

    Entry colorant_swatch_name;
    colorant_swatch_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "Colorants[1]/xmpG:swatchName");
    colorant_swatch_name.value = make_text(store.arena(), "Process Cyan",
                                           TextEncoding::Utf8);
    colorant_swatch_name.origin.block          = block;
    colorant_swatch_name.origin.order_in_block = 12;
    (void)store.add_entry(colorant_swatch_name);

    Entry colorant_mode;
    colorant_mode.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "Colorants[1]/xmpG:mode");
    colorant_mode.value = make_text(store.arena(), "CMYK",
                                    TextEncoding::Utf8);
    colorant_mode.origin.block          = block;
    colorant_mode.origin.order_in_block = 13;
    (void)store.add_entry(colorant_mode);

    Entry flat_swatch_groups;
    flat_swatch_groups.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups");
    flat_swatch_groups.value = make_text(store.arena(), "legacy-swatch-groups",
                                         TextEncoding::Utf8);
    flat_swatch_groups.origin.block          = block;
    flat_swatch_groups.origin.order_in_block = 14;
    (void)store.add_entry(flat_swatch_groups);

    Entry swatch_group_name;
    swatch_group_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups[1]/xmpG:groupName");
    swatch_group_name.value = make_text(store.arena(), "Brand Colors",
                                        TextEncoding::Utf8);
    swatch_group_name.origin.block          = block;
    swatch_group_name.origin.order_in_block = 15;
    (void)store.add_entry(swatch_group_name);

    Entry swatch_group_type;
    swatch_group_type.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups[1]/xmpG:groupType");
    swatch_group_type.value = make_text(store.arena(), "1",
                                        TextEncoding::Utf8);
    swatch_group_type.origin.block          = block;
    swatch_group_type.origin.order_in_block = 16;
    (void)store.add_entry(swatch_group_type);

    Entry flat_swatch_group_colorants;
    flat_swatch_group_colorants.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups[1]/Colorants");
    flat_swatch_group_colorants.value = make_text(
        store.arena(), "legacy-group-colorants", TextEncoding::Utf8);
    flat_swatch_group_colorants.origin.block          = block;
    flat_swatch_group_colorants.origin.order_in_block = 17;
    (void)store.add_entry(flat_swatch_group_colorants);

    Entry swatch_group_colorant_name;
    swatch_group_colorant_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups[1]/Colorants[1]/xmpG:swatchName");
    swatch_group_colorant_name.value = make_text(
        store.arena(), "Accent Orange", TextEncoding::Utf8);
    swatch_group_colorant_name.origin.block          = block;
    swatch_group_colorant_name.origin.order_in_block = 18;
    (void)store.add_entry(swatch_group_colorant_name);

    Entry swatch_group_colorant_mode;
    swatch_group_colorant_mode.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups[1]/Colorants[1]/xmpG:mode");
    swatch_group_colorant_mode.value = make_text(
        store.arena(), "RGB", TextEncoding::Utf8);
    swatch_group_colorant_mode.origin.block          = block;
    swatch_group_colorant_mode.origin.order_in_block = 19;
    (void)store.add_entry(swatch_group_colorant_mode);

    Entry flat_project_ref;
    flat_project_ref.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "ProjectRef");
    flat_project_ref.value = make_text(store.arena(), "legacy-project-ref",
                                       TextEncoding::Utf8);
    flat_project_ref.origin.block          = block;
    flat_project_ref.origin.order_in_block = 20;
    (void)store.add_entry(flat_project_ref);

    Entry project_ref_path;
    project_ref_path.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "ProjectRef/path");
    project_ref_path.value = make_text(store.arena(), "/proj/edit.prproj",
                                       TextEncoding::Utf8);
    project_ref_path.origin.block          = block;
    project_ref_path.origin.order_in_block = 21;
    (void)store.add_entry(project_ref_path);

    Entry project_ref_type;
    project_ref_type.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "ProjectRef/type");
    project_ref_type.value = make_text(store.arena(), "movie",
                                       TextEncoding::Utf8);
    project_ref_type.origin.block          = block;
    project_ref_type.origin.order_in_block = 22;
    (void)store.add_entry(project_ref_type);

    Entry flat_alt_timecode;
    flat_alt_timecode.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "altTimecode");
    flat_alt_timecode.value = make_text(store.arena(), "legacy-alt-timecode",
                                        TextEncoding::Utf8);
    flat_alt_timecode.origin.block          = block;
    flat_alt_timecode.origin.order_in_block = 23;
    (void)store.add_entry(flat_alt_timecode);

    Entry alt_timecode_time_format;
    alt_timecode_time_format.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "altTimecode/timeFormat");
    alt_timecode_time_format.value = make_text(store.arena(),
                                               "2997DropTimecode",
                                               TextEncoding::Utf8);
    alt_timecode_time_format.origin.block          = block;
    alt_timecode_time_format.origin.order_in_block = 24;
    (void)store.add_entry(alt_timecode_time_format);

    Entry alt_timecode_time_value;
    alt_timecode_time_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "altTimecode/timeValue");
    alt_timecode_time_value.value = make_text(store.arena(), "00:00:10:12",
                                              TextEncoding::Utf8);
    alt_timecode_time_value.origin.block          = block;
    alt_timecode_time_value.origin.order_in_block = 25;
    (void)store.add_entry(alt_timecode_time_value);

    Entry alt_timecode_value;
    alt_timecode_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "altTimecode/value");
    alt_timecode_value.value = make_text(store.arena(), "312",
                                         TextEncoding::Utf8);
    alt_timecode_value.origin.block          = block;
    alt_timecode_value.origin.order_in_block = 26;
    (void)store.add_entry(alt_timecode_value);

    Entry flat_start_timecode;
    flat_start_timecode.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "startTimecode");
    flat_start_timecode.value = make_text(store.arena(),
                                          "legacy-start-timecode",
                                          TextEncoding::Utf8);
    flat_start_timecode.origin.block          = block;
    flat_start_timecode.origin.order_in_block = 27;
    (void)store.add_entry(flat_start_timecode);

    Entry start_timecode_time_format;
    start_timecode_time_format.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "startTimecode/timeFormat");
    start_timecode_time_format.value = make_text(store.arena(),
                                                 "2997NonDropTimecode",
                                                 TextEncoding::Utf8);
    start_timecode_time_format.origin.block          = block;
    start_timecode_time_format.origin.order_in_block = 28;
    (void)store.add_entry(start_timecode_time_format);

    Entry start_timecode_time_value;
    start_timecode_time_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "startTimecode/timeValue");
    start_timecode_time_value.value = make_text(store.arena(), "01:00:00:00",
                                                TextEncoding::Utf8);
    start_timecode_time_value.origin.block          = block;
    start_timecode_time_value.origin.order_in_block = 29;
    (void)store.add_entry(start_timecode_time_value);

    Entry start_timecode_value;
    start_timecode_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "startTimecode/value");
    start_timecode_value.value = make_text(store.arena(), "107892",
                                           TextEncoding::Utf8);
    start_timecode_value.origin.block          = block;
    start_timecode_value.origin.order_in_block = 30;
    (void)store.add_entry(start_timecode_value);

    Entry flat_duration;
    flat_duration.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "duration");
    flat_duration.value = make_text(store.arena(), "legacy-duration",
                                    TextEncoding::Utf8);
    flat_duration.origin.block          = block;
    flat_duration.origin.order_in_block = 31;
    (void)store.add_entry(flat_duration);

    Entry duration_scale;
    duration_scale.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "duration/scale");
    duration_scale.value = make_text(store.arena(), "1/48000",
                                     TextEncoding::Utf8);
    duration_scale.origin.block          = block;
    duration_scale.origin.order_in_block = 32;
    (void)store.add_entry(duration_scale);

    Entry duration_value;
    duration_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "duration/value");
    duration_value.value = make_text(store.arena(), "96000",
                                     TextEncoding::Utf8);
    duration_value.origin.block          = block;
    duration_value.origin.order_in_block = 33;
    (void)store.add_entry(duration_value);

    Entry flat_intro_time;
    flat_intro_time.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "introTime");
    flat_intro_time.value = make_text(store.arena(), "legacy-intro-time",
                                      TextEncoding::Utf8);
    flat_intro_time.origin.block          = block;
    flat_intro_time.origin.order_in_block = 34;
    (void)store.add_entry(flat_intro_time);

    Entry intro_time_scale;
    intro_time_scale.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "introTime/scale");
    intro_time_scale.value = make_text(store.arena(), "1/1000",
                                       TextEncoding::Utf8);
    intro_time_scale.origin.block          = block;
    intro_time_scale.origin.order_in_block = 35;
    (void)store.add_entry(intro_time_scale);

    Entry intro_time_value;
    intro_time_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "introTime/value");
    intro_time_value.value = make_text(store.arena(), "2500",
                                       TextEncoding::Utf8);
    intro_time_value.origin.block          = block;
    intro_time_value.origin.order_in_block = 36;
    (void)store.add_entry(intro_time_value);

    Entry flat_out_cue;
    flat_out_cue.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "outCue");
    flat_out_cue.value = make_text(store.arena(), "legacy-out-cue",
                                   TextEncoding::Utf8);
    flat_out_cue.origin.block          = block;
    flat_out_cue.origin.order_in_block = 37;
    (void)store.add_entry(flat_out_cue);

    Entry out_cue_scale;
    out_cue_scale.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "outCue/scale");
    out_cue_scale.value = make_text(store.arena(), "1/1000",
                                    TextEncoding::Utf8);
    out_cue_scale.origin.block          = block;
    out_cue_scale.origin.order_in_block = 38;
    (void)store.add_entry(out_cue_scale);

    Entry out_cue_value;
    out_cue_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "outCue/value");
    out_cue_value.value = make_text(store.arena(), "18000",
                                    TextEncoding::Utf8);
    out_cue_value.origin.block          = block;
    out_cue_value.origin.order_in_block = 39;
    (void)store.add_entry(out_cue_value);

    Entry flat_relative_timestamp;
    flat_relative_timestamp.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "relativeTimestamp");
    flat_relative_timestamp.value = make_text(store.arena(),
                                              "legacy-relative-timestamp",
                                              TextEncoding::Utf8);
    flat_relative_timestamp.origin.block          = block;
    flat_relative_timestamp.origin.order_in_block = 40;
    (void)store.add_entry(flat_relative_timestamp);

    Entry relative_timestamp_scale;
    relative_timestamp_scale.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "relativeTimestamp/scale");
    relative_timestamp_scale.value = make_text(store.arena(), "1/1000",
                                               TextEncoding::Utf8);
    relative_timestamp_scale.origin.block          = block;
    relative_timestamp_scale.origin.order_in_block = 41;
    (void)store.add_entry(relative_timestamp_scale);

    Entry relative_timestamp_value;
    relative_timestamp_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "relativeTimestamp/value");
    relative_timestamp_value.value = make_text(store.arena(), "450",
                                               TextEncoding::Utf8);
    relative_timestamp_value.origin.block          = block;
    relative_timestamp_value.origin.order_in_block = 42;
    (void)store.add_entry(relative_timestamp_value);

    Entry flat_video_frame_size;
    flat_video_frame_size.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoFrameSize");
    flat_video_frame_size.value = make_text(store.arena(),
                                            "legacy-video-frame-size",
                                            TextEncoding::Utf8);
    flat_video_frame_size.origin.block          = block;
    flat_video_frame_size.origin.order_in_block = 43;
    (void)store.add_entry(flat_video_frame_size);

    Entry video_frame_size_w;
    video_frame_size_w.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoFrameSize/stDim:w");
    video_frame_size_w.value = make_text(store.arena(), "1920",
                                         TextEncoding::Utf8);
    video_frame_size_w.origin.block          = block;
    video_frame_size_w.origin.order_in_block = 44;
    (void)store.add_entry(video_frame_size_w);

    Entry video_frame_size_h;
    video_frame_size_h.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoFrameSize/stDim:h");
    video_frame_size_h.value = make_text(store.arena(), "1080",
                                         TextEncoding::Utf8);
    video_frame_size_h.origin.block          = block;
    video_frame_size_h.origin.order_in_block = 45;
    (void)store.add_entry(video_frame_size_h);

    Entry video_frame_size_unit;
    video_frame_size_unit.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoFrameSize/stDim:unit");
    video_frame_size_unit.value = make_text(store.arena(), "pixel",
                                            TextEncoding::Utf8);
    video_frame_size_unit.origin.block          = block;
    video_frame_size_unit.origin.order_in_block = 46;
    (void)store.add_entry(video_frame_size_unit);

    Entry flat_alpha_color;
    flat_alpha_color.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoAlphaPremultipleColor");
    flat_alpha_color.value = make_text(store.arena(), "legacy-alpha-color",
                                       TextEncoding::Utf8);
    flat_alpha_color.origin.block          = block;
    flat_alpha_color.origin.order_in_block = 47;
    (void)store.add_entry(flat_alpha_color);

    Entry alpha_color_mode;
    alpha_color_mode.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoAlphaPremultipleColor/xmpG:mode");
    alpha_color_mode.value = make_text(store.arena(), "RGB",
                                       TextEncoding::Utf8);
    alpha_color_mode.origin.block          = block;
    alpha_color_mode.origin.order_in_block = 48;
    (void)store.add_entry(alpha_color_mode);

    Entry alpha_color_red;
    alpha_color_red.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoAlphaPremultipleColor/xmpG:red");
    alpha_color_red.value = make_text(store.arena(), "255",
                                      TextEncoding::Utf8);
    alpha_color_red.origin.block          = block;
    alpha_color_red.origin.order_in_block = 49;
    (void)store.add_entry(alpha_color_red);

    Entry alpha_color_green;
    alpha_color_green.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoAlphaPremultipleColor/xmpG:green");
    alpha_color_green.value = make_text(store.arena(), "0",
                                        TextEncoding::Utf8);
    alpha_color_green.origin.block          = block;
    alpha_color_green.origin.order_in_block = 50;
    (void)store.add_entry(alpha_color_green);

    Entry alpha_color_blue;
    alpha_color_blue.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoAlphaPremultipleColor/xmpG:blue");
    alpha_color_blue.value = make_text(store.arena(), "255",
                                       TextEncoding::Utf8);
    alpha_color_blue.origin.block          = block;
    alpha_color_blue.origin.order_in_block = 51;
    (void)store.add_entry(alpha_color_blue);

    Entry flat_beat_splice_params;
    flat_beat_splice_params.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "beatSpliceParams");
    flat_beat_splice_params.value = make_text(
        store.arena(), "legacy-beat-splice", TextEncoding::Utf8);
    flat_beat_splice_params.origin.block          = block;
    flat_beat_splice_params.origin.order_in_block = 52;
    (void)store.add_entry(flat_beat_splice_params);

    Entry flat_rise_in_time_duration;
    flat_rise_in_time_duration.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "beatSpliceParams/riseInTimeDuration");
    flat_rise_in_time_duration.value = make_text(
        store.arena(), "legacy-rise-duration", TextEncoding::Utf8);
    flat_rise_in_time_duration.origin.block          = block;
    flat_rise_in_time_duration.origin.order_in_block = 53;
    (void)store.add_entry(flat_rise_in_time_duration);

    Entry rise_in_decibel;
    rise_in_decibel.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "beatSpliceParams/riseInDecibel");
    rise_in_decibel.value = make_text(store.arena(), "3.5",
                                      TextEncoding::Utf8);
    rise_in_decibel.origin.block          = block;
    rise_in_decibel.origin.order_in_block = 54;
    (void)store.add_entry(rise_in_decibel);

    Entry rise_in_time_duration_scale;
    rise_in_time_duration_scale.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "beatSpliceParams/riseInTimeDuration/scale");
    rise_in_time_duration_scale.value = make_text(
        store.arena(), "1/1000", TextEncoding::Utf8);
    rise_in_time_duration_scale.origin.block          = block;
    rise_in_time_duration_scale.origin.order_in_block = 55;
    (void)store.add_entry(rise_in_time_duration_scale);

    Entry rise_in_time_duration_value;
    rise_in_time_duration_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "beatSpliceParams/riseInTimeDuration/value");
    rise_in_time_duration_value.value = make_text(
        store.arena(), "1200", TextEncoding::Utf8);
    rise_in_time_duration_value.origin.block          = block;
    rise_in_time_duration_value.origin.order_in_block = 56;
    (void)store.add_entry(rise_in_time_duration_value);

    Entry use_file_beats_marker;
    use_file_beats_marker.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "beatSpliceParams/useFileBeatsMarker");
    use_file_beats_marker.value = make_text(
        store.arena(), "True", TextEncoding::Utf8);
    use_file_beats_marker.origin.block          = block;
    use_file_beats_marker.origin.order_in_block = 57;
    (void)store.add_entry(use_file_beats_marker);

    Entry flat_markers;
    flat_markers.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "markers");
    flat_markers.value = make_text(store.arena(), "legacy-markers",
                                   TextEncoding::Utf8);
    flat_markers.origin.block          = block;
    flat_markers.origin.order_in_block = 58;
    (void)store.add_entry(flat_markers);

    Entry flat_cue_point_params;
    flat_cue_point_params.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "markers/cuePointParams");
    flat_cue_point_params.value = make_text(
        store.arena(), "legacy-cuepoint", TextEncoding::Utf8);
    flat_cue_point_params.origin.block          = block;
    flat_cue_point_params.origin.order_in_block = 59;
    (void)store.add_entry(flat_cue_point_params);

    Entry marker_name;
    marker_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "markers/name");
    marker_name.value = make_text(store.arena(), "Verse 1",
                                  TextEncoding::Utf8);
    marker_name.origin.block          = block;
    marker_name.origin.order_in_block = 60;
    (void)store.add_entry(marker_name);

    Entry marker_start_time;
    marker_start_time.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "markers/startTime");
    marker_start_time.value = make_text(store.arena(), "00:00:05.000",
                                        TextEncoding::Utf8);
    marker_start_time.origin.block          = block;
    marker_start_time.origin.order_in_block = 61;
    (void)store.add_entry(marker_start_time);

    Entry cue_point_key;
    cue_point_key.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "markers/cuePointParams/key");
    cue_point_key.value = make_text(store.arena(), "chapter",
                                    TextEncoding::Utf8);
    cue_point_key.origin.block          = block;
    cue_point_key.origin.order_in_block = 62;
    (void)store.add_entry(cue_point_key);

    Entry cue_point_value;
    cue_point_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "markers/cuePointParams/value");
    cue_point_value.value = make_text(store.arena(), "intro",
                                      TextEncoding::Utf8);
    cue_point_value.origin.block          = block;
    cue_point_value.origin.order_in_block = 63;
    (void)store.add_entry(cue_point_value);

    Entry flat_resample_params;
    flat_resample_params.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "resampleParams");
    flat_resample_params.value = make_text(
        store.arena(), "legacy-resample", TextEncoding::Utf8);
    flat_resample_params.origin.block          = block;
    flat_resample_params.origin.order_in_block = 64;
    (void)store.add_entry(flat_resample_params);

    Entry resample_quality;
    resample_quality.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "resampleParams/quality");
    resample_quality.value = make_text(store.arena(), "high",
                                       TextEncoding::Utf8);
    resample_quality.origin.block          = block;
    resample_quality.origin.order_in_block = 65;
    (void)store.add_entry(resample_quality);

    Entry flat_time_scale_params;
    flat_time_scale_params.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "timeScaleParams");
    flat_time_scale_params.value = make_text(
        store.arena(), "legacy-time-scale-params", TextEncoding::Utf8);
    flat_time_scale_params.origin.block          = block;
    flat_time_scale_params.origin.order_in_block = 66;
    (void)store.add_entry(flat_time_scale_params);

    Entry time_scale_params_frame_overlap;
    time_scale_params_frame_overlap.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "timeScaleParams/frameOverlappingPercentage");
    time_scale_params_frame_overlap.value = make_text(
        store.arena(), "12.5", TextEncoding::Utf8);
    time_scale_params_frame_overlap.origin.block          = block;
    time_scale_params_frame_overlap.origin.order_in_block = 67;
    (void)store.add_entry(time_scale_params_frame_overlap);

    Entry time_scale_params_frame_size;
    time_scale_params_frame_size.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "timeScaleParams/frameSize");
    time_scale_params_frame_size.value = make_text(
        store.arena(), "48", TextEncoding::Utf8);
    time_scale_params_frame_size.origin.block          = block;
    time_scale_params_frame_size.origin.order_in_block = 68;
    (void)store.add_entry(time_scale_params_frame_size);

    Entry time_scale_params_quality;
    time_scale_params_quality.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "timeScaleParams/quality");
    time_scale_params_quality.value = make_text(
        store.arena(), "medium", TextEncoding::Utf8);
    time_scale_params_quality.origin.block          = block;
    time_scale_params_quality.origin.order_in_block = 69;
    (void)store.add_entry(time_scale_params_quality);

    Entry flat_contributed_media;
    flat_contributed_media.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "contributedMedia");
    flat_contributed_media.value = make_text(
        store.arena(), "legacy-contributed-media", TextEncoding::Utf8);
    flat_contributed_media.origin.block          = block;
    flat_contributed_media.origin.order_in_block = 70;
    (void)store.add_entry(flat_contributed_media);

    Entry flat_contributed_media_duration;
    flat_contributed_media_duration.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "contributedMedia[1]/duration");
    flat_contributed_media_duration.value = make_text(
        store.arena(), "legacy-contributed-duration", TextEncoding::Utf8);
    flat_contributed_media_duration.origin.block          = block;
    flat_contributed_media_duration.origin.order_in_block = 71;
    (void)store.add_entry(flat_contributed_media_duration);

    Entry flat_contributed_media_start_time;
    flat_contributed_media_start_time.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "contributedMedia[1]/startTime");
    flat_contributed_media_start_time.value = make_text(
        store.arena(), "legacy-contributed-start-time", TextEncoding::Utf8);
    flat_contributed_media_start_time.origin.block          = block;
    flat_contributed_media_start_time.origin.order_in_block = 72;
    (void)store.add_entry(flat_contributed_media_start_time);

    Entry contributed_media_path;
    contributed_media_path.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "contributedMedia[1]/path");
    contributed_media_path.value = make_text(
        store.arena(), "/media/broll.mov", TextEncoding::Utf8);
    contributed_media_path.origin.block          = block;
    contributed_media_path.origin.order_in_block = 73;
    (void)store.add_entry(contributed_media_path);

    Entry contributed_media_managed;
    contributed_media_managed.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "contributedMedia[1]/managed");
    contributed_media_managed.value = make_text(
        store.arena(), "True", TextEncoding::Utf8);
    contributed_media_managed.origin.block          = block;
    contributed_media_managed.origin.order_in_block = 74;
    (void)store.add_entry(contributed_media_managed);

    Entry contributed_media_track;
    contributed_media_track.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "contributedMedia[1]/track");
    contributed_media_track.value = make_text(
        store.arena(), "V1", TextEncoding::Utf8);
    contributed_media_track.origin.block          = block;
    contributed_media_track.origin.order_in_block = 75;
    (void)store.add_entry(contributed_media_track);

    Entry contributed_media_web_statement;
    contributed_media_web_statement.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "contributedMedia[1]/webStatement");
    contributed_media_web_statement.value = make_text(
        store.arena(), "https://example.test/media/broll",
        TextEncoding::Utf8);
    contributed_media_web_statement.origin.block          = block;
    contributed_media_web_statement.origin.order_in_block = 76;
    (void)store.add_entry(contributed_media_web_statement);

    Entry contributed_media_duration_scale;
    contributed_media_duration_scale.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "contributedMedia[1]/duration/scale");
    contributed_media_duration_scale.value = make_text(
        store.arena(), "1/24000", TextEncoding::Utf8);
    contributed_media_duration_scale.origin.block          = block;
    contributed_media_duration_scale.origin.order_in_block = 77;
    (void)store.add_entry(contributed_media_duration_scale);

    Entry contributed_media_duration_value;
    contributed_media_duration_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "contributedMedia[1]/duration/value");
    contributed_media_duration_value.value = make_text(
        store.arena(), "48000", TextEncoding::Utf8);
    contributed_media_duration_value.origin.block          = block;
    contributed_media_duration_value.origin.order_in_block = 78;
    (void)store.add_entry(contributed_media_duration_value);

    Entry contributed_media_start_time_scale;
    contributed_media_start_time_scale.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "contributedMedia[1]/startTime/scale");
    contributed_media_start_time_scale.value = make_text(
        store.arena(), "1/24000", TextEncoding::Utf8);
    contributed_media_start_time_scale.origin.block          = block;
    contributed_media_start_time_scale.origin.order_in_block = 79;
    (void)store.add_entry(contributed_media_start_time_scale);

    Entry contributed_media_start_time_value;
    contributed_media_start_time_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "contributedMedia[1]/startTime/value");
    contributed_media_start_time_value.value = make_text(
        store.arena(), "1200", TextEncoding::Utf8);
    contributed_media_start_time_value.origin.block          = block;
    contributed_media_start_time_value.origin.order_in_block = 80;
    (void)store.add_entry(contributed_media_start_time_value);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(16384);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("xmlns:xmpBJ=\"http://ns.adobe.com/xap/1.0/bj/\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:xmpTPg=\"http://ns.adobe.com/xap/1.0/t/pg/\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:xmpDM=\"http://ns.adobe.com/xmp/1.0/DynamicMedia/\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stJob=\"http://ns.adobe.com/xap/1.0/sType/Job#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stDim=\"http://ns.adobe.com/xap/1.0/sType/Dimensions#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stFnt=\"http://ns.adobe.com/xap/1.0/sType/Font#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:xmpG=\"http://ns.adobe.com/xap/1.0/g/\""),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpBJ:JobRef>"), std::string_view::npos);
    EXPECT_NE(s.find("<stJob:id>job-1</stJob:id>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<stJob:name>Layout Pass</stJob:name>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stJob:url>https://example.test/job/1</stJob:url>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpTPg:MaxPageSize rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<stDim:w>8.5</stDim:w>"), std::string_view::npos);
    EXPECT_NE(s.find("<stDim:h>11</stDim:h>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<stDim:unit>inch</stDim:unit>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpTPg:Fonts>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<stFnt:fontName>Source Serif</stFnt:fontName>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stFnt:childFontFiles>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li>SourceSerif-Regular.otf</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpTPg:Colorants>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpG:swatchName>Process Cyan</xmpG:swatchName>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpG:mode>CMYK</xmpG:mode>"), std::string_view::npos);
    EXPECT_NE(s.find("<xmpTPg:SwatchGroups>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpG:groupName>Brand Colors</xmpG:groupName>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpG:groupType>1</xmpG:groupType>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpTPg:Colorants>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpG:swatchName>Accent Orange</xmpG:swatchName>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpG:mode>RGB</xmpG:mode>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:ProjectRef rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:path>/proj/edit.prproj</xmpDM:path>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:type>movie</xmpDM:type>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:altTimecode rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:timeFormat>2997DropTimecode</xmpDM:timeFormat>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:timeValue>00:00:10:12</xmpDM:timeValue>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:value>312</xmpDM:value>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:startTimecode rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<xmpDM:timeFormat>2997NonDropTimecode</xmpDM:timeFormat>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:timeValue>01:00:00:00</xmpDM:timeValue>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:value>107892</xmpDM:value>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:duration rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:scale>1/48000</xmpDM:scale>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:value>96000</xmpDM:value>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:introTime rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:scale>1/1000</xmpDM:scale>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:value>2500</xmpDM:value>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:outCue rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:value>18000</xmpDM:value>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:relativeTimestamp rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:value>450</xmpDM:value>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:videoFrameSize rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<stDim:w>1920</stDim:w>"), std::string_view::npos);
    EXPECT_NE(s.find("<stDim:h>1080</stDim:h>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<stDim:unit>pixel</stDim:unit>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:videoAlphaPremultipleColor rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpG:mode>RGB</xmpG:mode>"), std::string_view::npos);
    EXPECT_NE(s.find("<xmpG:red>255</xmpG:red>"), std::string_view::npos);
    EXPECT_NE(s.find("<xmpG:green>0</xmpG:green>"), std::string_view::npos);
    EXPECT_NE(s.find("<xmpG:blue>255</xmpG:blue>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:beatSpliceParams rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:riseInDecibel>3.5</xmpDM:riseInDecibel>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:riseInTimeDuration rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:scale>1/1000</xmpDM:scale>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:value>1200</xmpDM:value>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:useFileBeatsMarker>True</xmpDM:useFileBeatsMarker>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:markers rdf:parseType=\"Resource\">"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:name>Verse 1</xmpDM:name>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:startTime>00:00:05.000</xmpDM:startTime>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:cuePointParams rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:key>chapter</xmpDM:key>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:value>intro</xmpDM:value>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:resampleParams rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:quality>high</xmpDM:quality>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:timeScaleParams rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<xmpDM:frameOverlappingPercentage>12.5</xmpDM:frameOverlappingPercentage>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:frameSize>48</xmpDM:frameSize>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:quality>medium</xmpDM:quality>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:contributedMedia>"), std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:path>/media/broll.mov</xmpDM:path>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:managed>True</xmpDM:managed>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:track>V1</xmpDM:track>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<xmpDM:webStatement>https://example.test/media/broll</xmpDM:webStatement>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:duration rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:scale>1/24000</xmpDM:scale>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:value>48000</xmpDM:value>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:startTime rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:value>1200</xmpDM:value>"),
              std::string_view::npos);

    EXPECT_EQ(s.find("legacy-job-ref"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-page-size"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-fonts"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-colorants"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-swatch-groups"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-group-colorants"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-project-ref"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-alt-timecode"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-start-timecode"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-duration"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-intro-time"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-out-cue"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-relative-timestamp"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-video-frame-size"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-alpha-color"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-beat-splice"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-rise-duration"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-markers"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-cuepoint"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-resample"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-time-scale-params"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-contributed-media"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-contributed-duration"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-contributed-start-time"),
              std::string_view::npos);
    EXPECT_EQ(
        s.find("<stFnt:childFontFiles>SourceSerif-Regular.otf</stFnt:childFontFiles>"),
        std::string_view::npos);
}

TEST(XmpDump, PortablePromotesLegacyAdobeStructuredChildPrefixes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry derived_document_id;
    derived_document_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "DerivedFrom/documentID");
    derived_document_id.value = make_text(store.arena(), "xmp.did:base",
                                          TextEncoding::Utf8);
    derived_document_id.origin.block          = block;
    derived_document_id.origin.order_in_block = 0;
    (void)store.add_entry(derived_document_id);

    Entry job_ref_id;
    job_ref_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/bj/",
        "JobRef[1]/id");
    job_ref_id.value = make_text(store.arena(), "job-1",
                                 TextEncoding::Utf8);
    job_ref_id.origin.block          = block;
    job_ref_id.origin.order_in_block = 1;
    (void)store.add_entry(job_ref_id);

    Entry max_page_size_w;
    max_page_size_w.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "MaxPageSize/w");
    max_page_size_w.value = make_text(store.arena(), "8.5",
                                      TextEncoding::Utf8);
    max_page_size_w.origin.block          = block;
    max_page_size_w.origin.order_in_block = 2;
    (void)store.add_entry(max_page_size_w);

    Entry font_name;
    font_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "Fonts[1]/fontName");
    font_name.value = make_text(store.arena(), "Source Serif",
                                TextEncoding::Utf8);
    font_name.origin.block          = block;
    font_name.origin.order_in_block = 3;
    (void)store.add_entry(font_name);

    Entry swatch_group_name;
    swatch_group_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups[1]/groupName");
    swatch_group_name.value = make_text(store.arena(), "Brand Colors",
                                        TextEncoding::Utf8);
    swatch_group_name.origin.block          = block;
    swatch_group_name.origin.order_in_block = 4;
    (void)store.add_entry(swatch_group_name);

    Entry swatch_group_colorant_name;
    swatch_group_colorant_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups[1]/Colorants[1]/swatchName");
    swatch_group_colorant_name.value = make_text(
        store.arena(), "Accent Orange", TextEncoding::Utf8);
    swatch_group_colorant_name.origin.block          = block;
    swatch_group_colorant_name.origin.order_in_block = 5;
    (void)store.add_entry(swatch_group_colorant_name);

    Entry manifest_file_path;
    manifest_file_path.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Manifest[1]/reference/filePath");
    manifest_file_path.value = make_text(store.arena(),
                                         "/tmp/manifest.dat",
                                         TextEncoding::Utf8);
    manifest_file_path.origin.block          = block;
    manifest_file_path.origin.order_in_block = 6;
    (void)store.add_entry(manifest_file_path);

    Entry versions_event_action;
    versions_event_action.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/event/action");
    versions_event_action.value = make_text(store.arena(), "saved",
                                            TextEncoding::Utf8);
    versions_event_action.origin.block          = block;
    versions_event_action.origin.order_in_block = 7;
    (void)store.add_entry(versions_event_action);

    Entry video_frame_size_w;
    video_frame_size_w.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoFrameSize/w");
    video_frame_size_w.value = make_text(store.arena(), "1920",
                                         TextEncoding::Utf8);
    video_frame_size_w.origin.block          = block;
    video_frame_size_w.origin.order_in_block = 8;
    (void)store.add_entry(video_frame_size_w);

    Entry alpha_color_mode;
    alpha_color_mode.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoAlphaPremultipleColor/mode");
    alpha_color_mode.value = make_text(store.arena(), "RGB",
                                       TextEncoding::Utf8);
    alpha_color_mode.origin.block          = block;
    alpha_color_mode.origin.order_in_block = 9;
    (void)store.add_entry(alpha_color_mode);

    Entry rendition_manage_to;
    rendition_manage_to.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "RenditionOf/manageTo");
    rendition_manage_to.value = make_text(
        store.arena(), "https://example.invalid/rendition",
        TextEncoding::Utf8);
    rendition_manage_to.origin.block          = block;
    rendition_manage_to.origin.order_in_block = 10;
    (void)store.add_entry(rendition_manage_to);

    Entry manifest_manage_ui;
    manifest_manage_ui.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Manifest[1]/reference/manageUI");
    manifest_manage_ui.value = make_text(
        store.arena(), "https://example.invalid/manage",
        TextEncoding::Utf8);
    manifest_manage_ui.origin.block          = block;
    manifest_manage_ui.origin.order_in_block = 11;
    (void)store.add_entry(manifest_manage_ui);

    Entry history_software_agent;
    history_software_agent.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "History[1]/softwareAgent");
    history_software_agent.value = make_text(
        store.arena(), "OpenMeta", TextEncoding::Utf8);
    history_software_agent.origin.block          = block;
    history_software_agent.origin.order_in_block = 12;
    (void)store.add_entry(history_software_agent);

    Entry versions_event_parameters;
    versions_event_parameters.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/event/parameters");
    versions_event_parameters.value = make_text(
        store.arena(), "chapter=1", TextEncoding::Utf8);
    versions_event_parameters.origin.block          = block;
    versions_event_parameters.origin.order_in_block = 13;
    (void)store.add_entry(versions_event_parameters);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("xmlns:stRef=\"http://ns.adobe.com/xap/1.0/sType/ResourceRef#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stJob=\"http://ns.adobe.com/xap/1.0/sType/Job#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stDim=\"http://ns.adobe.com/xap/1.0/sType/Dimensions#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stFnt=\"http://ns.adobe.com/xap/1.0/sType/Font#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stMfs=\"http://ns.adobe.com/xap/1.0/sType/ManifestItem#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stVer=\"http://ns.adobe.com/xap/1.0/sType/Version#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:stEvt=\"http://ns.adobe.com/xap/1.0/sType/ResourceEvent#\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("xmlns:xmpG=\"http://ns.adobe.com/xap/1.0/g/\""),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:documentID>xmp.did:base</stRef:documentID>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<stJob:id>job-1</stJob:id>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<stDim:w>8.5</stDim:w>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<stFnt:fontName>Source Serif</stFnt:fontName>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpG:groupName>Brand Colors</xmpG:groupName>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpG:swatchName>Accent Orange</xmpG:swatchName>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stMfs:reference rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:filePath>/tmp/manifest.dat</stRef:filePath>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<stRef:manageUI>https://example.invalid/manage</stRef:manageUI>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stVer:event rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stEvt:action>saved</stEvt:action>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stEvt:parameters>chapter=1</stEvt:parameters>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<stDim:w>1920</stDim:w>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpG:mode>RGB</xmpG:mode>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<stRef:manageTo>https://example.invalid/rendition</stRef:manageTo>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stEvt:softwareAgent>OpenMeta</stEvt:softwareAgent>"),
        std::string_view::npos);

    EXPECT_EQ(s.find("<xmpMM:documentID>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpBJ:id>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpTPg:w>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpTPg:fontName>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpTPg:groupName>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpTPg:swatchName>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:reference rdf:parseType=\"Resource\">"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:filePath>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:event rdf:parseType=\"Resource\">"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:action>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:manageTo>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:manageUI>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:softwareAgent>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:parameters>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpDM:w>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmpDM:mode>"), std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesXmpDmTracksStructuredFamily)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry flat_tracks;
    flat_tracks.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "Tracks");
    flat_tracks.value = make_text(store.arena(), "legacy-tracks",
                                  TextEncoding::Utf8);
    flat_tracks.origin.block          = block;
    flat_tracks.origin.order_in_block = 0;
    (void)store.add_entry(flat_tracks);

    Entry flat_track_markers;
    flat_track_markers.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "Tracks[1]/markers");
    flat_track_markers.value = make_text(store.arena(),
                                         "legacy-track-markers",
                                         TextEncoding::Utf8);
    flat_track_markers.origin.block          = block;
    flat_track_markers.origin.order_in_block = 1;
    (void)store.add_entry(flat_track_markers);

    Entry flat_track_marker_cue_point_params;
    flat_track_marker_cue_point_params.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "Tracks[1]/markers/cuePointParams");
    flat_track_marker_cue_point_params.value = make_text(
        store.arena(), "legacy-track-cue-point-params",
        TextEncoding::Utf8);
    flat_track_marker_cue_point_params.origin.block          = block;
    flat_track_marker_cue_point_params.origin.order_in_block = 2;
    (void)store.add_entry(flat_track_marker_cue_point_params);

    Entry track_name;
    track_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "Tracks[1]/trackName");
    track_name.value = make_text(store.arena(), "Dialogue",
                                 TextEncoding::Utf8);
    track_name.origin.block          = block;
    track_name.origin.order_in_block = 3;
    (void)store.add_entry(track_name);

    Entry track_type;
    track_type.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "Tracks[1]/trackType");
    track_type.value = make_text(store.arena(), "Audio",
                                 TextEncoding::Utf8);
    track_type.origin.block          = block;
    track_type.origin.order_in_block = 4;
    (void)store.add_entry(track_type);

    Entry track_frame_rate;
    track_frame_rate.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "Tracks[1]/frameRate");
    track_frame_rate.value = make_text(store.arena(), "f24000",
                                       TextEncoding::Utf8);
    track_frame_rate.origin.block          = block;
    track_frame_rate.origin.order_in_block = 5;
    (void)store.add_entry(track_frame_rate);

    Entry track_marker_name;
    track_marker_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "Tracks[1]/markers/name");
    track_marker_name.value = make_text(store.arena(), "Scene 1",
                                        TextEncoding::Utf8);
    track_marker_name.origin.block          = block;
    track_marker_name.origin.order_in_block = 6;
    (void)store.add_entry(track_marker_name);

    Entry track_marker_start_time;
    track_marker_start_time.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "Tracks[1]/markers/startTime");
    track_marker_start_time.value = make_text(
        store.arena(), "00:00:01.000", TextEncoding::Utf8);
    track_marker_start_time.origin.block          = block;
    track_marker_start_time.origin.order_in_block = 7;
    (void)store.add_entry(track_marker_start_time);

    Entry track_marker_cue_point_key;
    track_marker_cue_point_key.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "Tracks[1]/markers/cuePointParams/key");
    track_marker_cue_point_key.value = make_text(
        store.arena(), "chapter", TextEncoding::Utf8);
    track_marker_cue_point_key.origin.block          = block;
    track_marker_cue_point_key.origin.order_in_block = 8;
    (void)store.add_entry(track_marker_cue_point_key);

    Entry track_marker_cue_point_value;
    track_marker_cue_point_value.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "Tracks[1]/markers/cuePointParams/value");
    track_marker_cue_point_value.value = make_text(
        store.arena(), "scene-1", TextEncoding::Utf8);
    track_marker_cue_point_value.origin.block          = block;
    track_marker_cue_point_value.origin.order_in_block = 9;
    (void)store.add_entry(track_marker_cue_point_value);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("xmlns:xmpDM=\"http://ns.adobe.com/xmp/1.0/DynamicMedia/\""),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:Tracks>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:trackName>Dialogue</xmpDM:trackName>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:trackType>Audio</xmpDM:trackType>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:frameRate>f24000</xmpDM:frameRate>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:markers rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:name>Scene 1</xmpDM:name>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:startTime>00:00:01.000</xmpDM:startTime>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpDM:cuePointParams rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:key>chapter</xmpDM:key>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpDM:value>scene-1</xmpDM:value>"),
              std::string_view::npos);

    EXPECT_EQ(s.find("legacy-tracks"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-track-markers"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-track-cue-point-params"), std::string_view::npos);
}

TEST(XmpDump, PortablePromotesLegacyXmpMmPantryFormatToDcNamespace)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry pantry_instance_id;
    pantry_instance_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Pantry[1]/InstanceID");
    pantry_instance_id.value = make_text(store.arena(), "uuid:pantry-1",
                                         TextEncoding::Utf8);
    pantry_instance_id.origin.block          = block;
    pantry_instance_id.origin.order_in_block = 0;
    (void)store.add_entry(pantry_instance_id);

    Entry pantry_format;
    pantry_format.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Pantry[1]/format");
    pantry_format.value = make_text(store.arena(), "image/jpeg",
                                    TextEncoding::Utf8);
    pantry_format.origin.block          = block;
    pantry_format.origin.order_in_block = 1;
    (void)store.add_entry(pantry_format);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<xmpMM:Pantry>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<xmpMM:InstanceID>uuid:pantry-1</xmpMM:InstanceID>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<dc:format>image/jpeg</dc:format>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpMM:format>image/jpeg</xmpMM:format>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePreservesAuxStandardNamespace)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry lens;
    lens.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/exif/1.0/aux/", "Lens");
    lens.value = make_text(store.arena(), "RF24-70mm F2.8 L IS USM",
                           TextEncoding::Utf8);
    lens.origin.block          = block;
    lens.origin.order_in_block = 0;
    (void)store.add_entry(lens);

    Entry serial_number;
    serial_number.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/exif/1.0/aux/", "SerialNumber");
    serial_number.value = make_text(store.arena(), "1234567890",
                                    TextEncoding::Utf8);
    serial_number.origin.block          = block;
    serial_number.origin.order_in_block = 1;
    (void)store.add_entry(serial_number);

    Entry lens_id;
    lens_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/exif/1.0/aux/", "LensID");
    lens_id.value = make_text(store.arena(), "RF24-70",
                              TextEncoding::Utf8);
    lens_id.origin.block          = block;
    lens_id.origin.order_in_block = 2;
    (void)store.add_entry(lens_id);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("xmlns:aux=\"http://ns.adobe.com/exif/1.0/aux/\""),
              std::string_view::npos);
    EXPECT_NE(s.find("<aux:Lens>RF24-70mm F2.8 L IS USM</aux:Lens>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<aux:SerialNumber>1234567890</aux:SerialNumber>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<aux:LensID>RF24-70</aux:LensID>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableExistingStructuredIptc4xmpCoreEmitsResource)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry email;
    email.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiEmailWork");
    email.value = make_text(store.arena(), "editor@example.test",
                            TextEncoding::Utf8);
    email.origin.block          = block;
    email.origin.order_in_block = 0;
    (void)store.add_entry(email);

    Entry url;
    url.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiUrlWork");
    url.value = make_text(store.arena(), "https://example.test/contact",
                          TextEncoding::Utf8);
    url.origin.block          = block;
    url.origin.order_in_block = 1;
    (void)store.add_entry(url);

    Entry city;
    city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "LocationCreated/City");
    city.value = make_text(store.arena(), "Paris", TextEncoding::Utf8);
    city.origin.block          = block;
    city.origin.order_in_block = 2;
    (void)store.add_entry(city);

    Entry country;
    country.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "LocationCreated/CountryName");
    country.value = make_text(store.arena(), "France", TextEncoding::Utf8);
    country.origin.block          = block;
    country.origin.order_in_block = 3;
    (void)store.add_entry(country);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CiUrlWork>https://example.test/contact</Iptc4xmpCore:CiUrlWork>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:City>Paris</Iptc4xmpCore:City>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CountryName>France</Iptc4xmpCore:CountryName>"),
        std::string_view::npos);
}

TEST(XmpDump, PortablePreservesPlusNamespaceAndIndexedStructuredResources)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry name1;
    name1.key = make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee[1]/LicenseeName");
    name1.value = make_text(store.arena(), "Example Archive",
                            TextEncoding::Utf8);
    name1.origin.block          = block;
    name1.origin.order_in_block = 0;
    (void)store.add_entry(name1);

    Entry url1;
    url1.key = make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee[1]/LicenseeURL");
    url1.value = make_text(store.arena(), "https://example.test/archive",
                           TextEncoding::Utf8);
    url1.origin.block          = block;
    url1.origin.order_in_block = 1;
    (void)store.add_entry(url1);

    Entry name2;
    name2.key = make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee[2]/LicenseeName");
    name2.value = make_text(store.arena(), "Editorial Partner",
                            TextEncoding::Utf8);
    name2.origin.block          = block;
    name2.origin.order_in_block = 2;
    (void)store.add_entry(name2);

    Entry id2;
    id2.key = make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee[2]/LicenseeID");
    id2.value = make_text(store.arena(), "lic-002", TextEncoding::Utf8);
    id2.origin.block          = block;
    id2.origin.order_in_block = 3;
    (void)store.add_entry(id2);

    Entry constraints1;
    constraints1.key = make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "ImageAlterationConstraints[1]");
    constraints1.value = make_text(store.arena(), "No compositing",
                                   TextEncoding::Utf8);
    constraints1.origin.block          = block;
    constraints1.origin.order_in_block = 4;
    (void)store.add_entry(constraints1);

    Entry constraints2;
    constraints2.key = make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "ImageAlterationConstraints[2]");
    constraints2.value = make_text(store.arena(), "No AI upscaling",
                                   TextEncoding::Utf8);
    constraints2.origin.block          = block;
    constraints2.origin.order_in_block = 5;
    (void)store.add_entry(constraints2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("xmlns:plus=\"http://ns.useplus.org/ldf/xmp/1.0/\""),
              std::string_view::npos);
    EXPECT_NE(s.find("<plus:Licensee>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li rdf:parseType=\"Resource\">"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<plus:LicenseeName>Example Archive</plus:LicenseeName>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<plus:LicenseeURL>https://example.test/archive</plus:LicenseeURL>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<plus:LicenseeName>Editorial Partner</plus:LicenseeName>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<plus:LicenseeID>lic-002</plus:LicenseeID>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<plus:ImageAlterationConstraints>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>No compositing</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>No AI upscaling</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePreservesIptc4xmpExtIndexedStructuredResources)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry city1;
    city1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/City");
    city1.value = make_text(store.arena(), "Paris", TextEncoding::Utf8);
    city1.origin.block          = block;
    city1.origin.order_in_block = 0;
    (void)store.add_entry(city1);

    Entry country1;
    country1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/CountryName");
    country1.value = make_text(store.arena(), "France", TextEncoding::Utf8);
    country1.origin.block          = block;
    country1.origin.order_in_block = 1;
    (void)store.add_entry(country1);

    Entry city2;
    city2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[2]/City");
    city2.value = make_text(store.arena(), "Kyoto", TextEncoding::Utf8);
    city2.origin.block          = block;
    city2.origin.order_in_block = 2;
    (void)store.add_entry(city2);

    Entry country2;
    country2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[2]/CountryName");
    country2.value = make_text(store.arena(), "Japan", TextEncoding::Utf8);
    country2.origin.block          = block;
    country2.origin.order_in_block = 3;
    (void)store.add_entry(country2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("xmlns:Iptc4xmpExt=\"http://iptc.org/std/Iptc4xmpExt/2008-02-29/\""),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:City>Paris</Iptc4xmpExt:City>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:CountryName>France</Iptc4xmpExt:CountryName>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"),
        std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesKnownStructuredBaseShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry bad_contact_scalar;
    bad_contact_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo");
    bad_contact_scalar.value = make_text(store.arena(), "legacy-flat-contact",
                                         TextEncoding::Utf8);
    bad_contact_scalar.origin.block          = block;
    bad_contact_scalar.origin.order_in_block = 0;
    (void)store.add_entry(bad_contact_scalar);

    Entry good_contact_email;
    good_contact_email.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiEmailWork");
    good_contact_email.value = make_text(store.arena(),
                                         "editor@example.test",
                                         TextEncoding::Utf8);
    good_contact_email.origin.block          = block;
    good_contact_email.origin.order_in_block = 1;
    (void)store.add_entry(good_contact_email);

    Entry bad_location_structured;
    bad_location_structured.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown/City");
    bad_location_structured.value = make_text(store.arena(), "LegacyParis",
                                              TextEncoding::Utf8);
    bad_location_structured.origin.block          = block;
    bad_location_structured.origin.order_in_block = 2;
    (void)store.add_entry(bad_location_structured);

    Entry good_location_city;
    good_location_city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/City");
    good_location_city.value = make_text(store.arena(), "Paris",
                                         TextEncoding::Utf8);
    good_location_city.origin.block          = block;
    good_location_city.origin.order_in_block = 3;
    (void)store.add_entry(good_location_city);

    Entry bad_licensee_scalar;
    bad_licensee_scalar.key = make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee");
    bad_licensee_scalar.value = make_text(store.arena(), "legacy-licensee",
                                          TextEncoding::Utf8);
    bad_licensee_scalar.origin.block          = block;
    bad_licensee_scalar.origin.order_in_block = 4;
    (void)store.add_entry(bad_licensee_scalar);

    Entry good_licensee_name;
    good_licensee_name.key = make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee[1]/LicenseeName");
    good_licensee_name.value = make_text(store.arena(), "Example Archive",
                                         TextEncoding::Utf8);
    good_licensee_name.origin.block          = block;
    good_licensee_name.origin.order_in_block = 5;
    (void)store.add_entry(good_licensee_name);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:City>Paris</Iptc4xmpExt:City>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<plus:Licensee>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<plus:LicenseeName>Example Archive</plus:LicenseeName>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("legacy-flat-contact"), std::string_view::npos);
    EXPECT_EQ(s.find("LegacyParis"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-licensee"), std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesRemainingStructuredIndexedBaseShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry bad_creator_scalar;
    bad_creator_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator");
    bad_creator_scalar.value = make_text(store.arena(), "legacy-creator-base",
                                         TextEncoding::Utf8);
    bad_creator_scalar.origin.block          = block;
    bad_creator_scalar.origin.order_in_block = 0;
    (void)store.add_entry(bad_creator_scalar);

    Entry creator_name;
    creator_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Name[@xml:lang=x-default]");
    creator_name.value = make_text(store.arena(), "Alice Example",
                                   TextEncoding::Utf8);
    creator_name.origin.block          = block;
    creator_name.origin.order_in_block = 1;
    (void)store.add_entry(creator_name);

    Entry bad_contributor_scalar;
    bad_contributor_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Contributor");
    bad_contributor_scalar.value = make_text(
        store.arena(), "legacy-contributor-base", TextEncoding::Utf8);
    bad_contributor_scalar.origin.block          = block;
    bad_contributor_scalar.origin.order_in_block = 2;
    (void)store.add_entry(bad_contributor_scalar);

    Entry contributor_name;
    contributor_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Contributor[1]/Name[@xml:lang=x-default]");
    contributor_name.value = make_text(store.arena(), "Desk Editor",
                                       TextEncoding::Utf8);
    contributor_name.origin.block          = block;
    contributor_name.origin.order_in_block = 3;
    (void)store.add_entry(contributor_name);

    Entry contributor_role;
    contributor_role.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Contributor[1]/Role[1]");
    contributor_role.value = make_text(store.arena(), "editor",
                                       TextEncoding::Utf8);
    contributor_role.origin.block          = block;
    contributor_role.origin.order_in_block = 4;
    (void)store.add_entry(contributor_role);

    Entry bad_planning_scalar;
    bad_planning_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PlanningRef");
    bad_planning_scalar.value = make_text(
        store.arena(), "legacy-planning-base", TextEncoding::Utf8);
    bad_planning_scalar.origin.block          = block;
    bad_planning_scalar.origin.order_in_block = 5;
    (void)store.add_entry(bad_planning_scalar);

    Entry planning_name;
    planning_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PlanningRef[1]/Name[@xml:lang=x-default]");
    planning_name.value = make_text(store.arena(), "Editorial Plan",
                                    TextEncoding::Utf8);
    planning_name.origin.block          = block;
    planning_name.origin.order_in_block = 6;
    (void)store.add_entry(planning_name);

    Entry planning_role;
    planning_role.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PlanningRef[1]/Role[1]");
    planning_role.value = make_text(store.arena(), "assignment",
                                    TextEncoding::Utf8);
    planning_role.origin.block          = block;
    planning_role.origin.order_in_block = 7;
    (void)store.add_entry(planning_role);

    Entry bad_person_heard_scalar;
    bad_person_heard_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonHeard");
    bad_person_heard_scalar.value = make_text(
        store.arena(), "legacy-person-heard-base", TextEncoding::Utf8);
    bad_person_heard_scalar.origin.block          = block;
    bad_person_heard_scalar.origin.order_in_block = 8;
    (void)store.add_entry(bad_person_heard_scalar);

    Entry person_heard_name;
    person_heard_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonHeard[1]/Name[@xml:lang=x-default]");
    person_heard_name.value = make_text(store.arena(), "Witness",
                                        TextEncoding::Utf8);
    person_heard_name.origin.block          = block;
    person_heard_name.origin.order_in_block = 9;
    (void)store.add_entry(person_heard_name);

    Entry bad_shown_event_scalar;
    bad_shown_event_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ShownEvent");
    bad_shown_event_scalar.value = make_text(
        store.arena(), "legacy-shown-event-base", TextEncoding::Utf8);
    bad_shown_event_scalar.origin.block          = block;
    bad_shown_event_scalar.origin.order_in_block = 10;
    (void)store.add_entry(bad_shown_event_scalar);

    Entry shown_event_name;
    shown_event_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ShownEvent[1]/Name[@xml:lang=x-default]");
    shown_event_name.value = make_text(store.arena(), "Press Conference",
                                       TextEncoding::Utf8);
    shown_event_name.origin.block          = block;
    shown_event_name.origin.order_in_block = 11;
    (void)store.add_entry(shown_event_name);

    Entry bad_supply_chain_scalar;
    bad_supply_chain_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "SupplyChainSource");
    bad_supply_chain_scalar.value = make_text(
        store.arena(), "legacy-supply-chain-base", TextEncoding::Utf8);
    bad_supply_chain_scalar.origin.block          = block;
    bad_supply_chain_scalar.origin.order_in_block = 12;
    (void)store.add_entry(bad_supply_chain_scalar);

    Entry supply_chain_name;
    supply_chain_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "SupplyChainSource[1]/Name[@xml:lang=x-default]");
    supply_chain_name.value = make_text(store.arena(), "Agency Feed",
                                        TextEncoding::Utf8);
    supply_chain_name.origin.block          = block;
    supply_chain_name.origin.order_in_block = 13;
    (void)store.add_entry(supply_chain_name);

    Entry bad_video_shot_scalar;
    bad_video_shot_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "VideoShotType");
    bad_video_shot_scalar.value = make_text(
        store.arena(), "legacy-video-shot-base", TextEncoding::Utf8);
    bad_video_shot_scalar.origin.block          = block;
    bad_video_shot_scalar.origin.order_in_block = 14;
    (void)store.add_entry(bad_video_shot_scalar);

    Entry video_shot_name;
    video_shot_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "VideoShotType[1]/Name[@xml:lang=x-default]");
    video_shot_name.value = make_text(store.arena(), "Interview",
                                      TextEncoding::Utf8);
    video_shot_name.origin.block          = block;
    video_shot_name.origin.order_in_block = 15;
    (void)store.add_entry(video_shot_name);

    Entry bad_dopesheet_scalar;
    bad_dopesheet_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "DopesheetLink");
    bad_dopesheet_scalar.value = make_text(
        store.arena(), "legacy-dopesheet-base", TextEncoding::Utf8);
    bad_dopesheet_scalar.origin.block          = block;
    bad_dopesheet_scalar.origin.order_in_block = 16;
    (void)store.add_entry(bad_dopesheet_scalar);

    Entry dopesheet_qualifier;
    dopesheet_qualifier.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "DopesheetLink[1]/LinkQualifier[1]");
    dopesheet_qualifier.value = make_text(store.arena(), "keyframe",
                                          TextEncoding::Utf8);
    dopesheet_qualifier.origin.block          = block;
    dopesheet_qualifier.origin.order_in_block = 17;
    (void)store.add_entry(dopesheet_qualifier);

    Entry bad_snapshot_scalar;
    bad_snapshot_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Snapshot");
    bad_snapshot_scalar.value = make_text(
        store.arena(), "legacy-snapshot-base", TextEncoding::Utf8);
    bad_snapshot_scalar.origin.block          = block;
    bad_snapshot_scalar.origin.order_in_block = 18;
    (void)store.add_entry(bad_snapshot_scalar);

    Entry snapshot_qualifier;
    snapshot_qualifier.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Snapshot[1]/LinkQualifier[1]");
    snapshot_qualifier.value = make_text(store.arena(), "frame-001",
                                         TextEncoding::Utf8);
    snapshot_qualifier.origin.block          = block;
    snapshot_qualifier.origin.order_in_block = 19;
    (void)store.add_entry(snapshot_qualifier);

    Entry bad_transcript_scalar;
    bad_transcript_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "TranscriptLink");
    bad_transcript_scalar.value = make_text(
        store.arena(), "legacy-transcript-base", TextEncoding::Utf8);
    bad_transcript_scalar.origin.block          = block;
    bad_transcript_scalar.origin.order_in_block = 20;
    (void)store.add_entry(bad_transcript_scalar);

    Entry transcript_qualifier;
    transcript_qualifier.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "TranscriptLink[1]/LinkQualifier[1]");
    transcript_qualifier.value = make_text(store.arena(), "quote",
                                           TextEncoding::Utf8);
    transcript_qualifier.origin.block          = block;
    transcript_qualifier.origin.order_in_block = 21;
    (void)store.add_entry(transcript_qualifier);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(16384);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Alice Example</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Desk Editor</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>editor</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Editorial Plan</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>assignment</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Witness</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Press Conference</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Agency Feed</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Interview</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>keyframe</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>frame-001</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>quote</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-creator-base"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-contributor-base"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-planning-base"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-person-heard-base"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-shown-event-base"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-supply-chain-base"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-video-shot-base"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-dopesheet-base"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-snapshot-base"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-transcript-base"), std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesKnownStructuredChildShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry bad_region_scalar;
    bad_region_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion");
    bad_region_scalar.value = make_text(store.arena(), "legacy-region",
                                        TextEncoding::Utf8);
    bad_region_scalar.origin.block          = block;
    bad_region_scalar.origin.order_in_block = 0;
    (void)store.add_entry(bad_region_scalar);

    Entry good_region_nested;
    good_region_nested.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceName");
    good_region_nested.value = make_text(store.arena(), "Tokyo",
                                         TextEncoding::Utf8);
    good_region_nested.origin.block          = block;
    good_region_nested.origin.order_in_block = 1;
    (void)store.add_entry(good_region_nested);

    Entry bad_address_scalar;
    bad_address_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address");
    bad_address_scalar.value = make_text(store.arena(), "legacy-address",
                                         TextEncoding::Utf8);
    bad_address_scalar.origin.block          = block;
    bad_address_scalar.origin.order_in_block = 2;
    (void)store.add_entry(bad_address_scalar);

    Entry good_address_nested;
    good_address_nested.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/City");
    good_address_nested.value = make_text(store.arena(), "Kyoto",
                                          TextEncoding::Utf8);
    good_address_nested.origin.block          = block;
    good_address_nested.origin.order_in_block = 3;
    (void)store.add_entry(good_address_nested);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"),
              std::string_view::npos);
    EXPECT_EQ(
        s.find("<Iptc4xmpExt:Address rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_EQ(s.find("legacy-region"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-address"), std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesKnownIndexedStructuredChildShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry bad_extadr_scalar;
    bad_extadr_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrExtadr");
    bad_extadr_scalar.value = make_text(store.arena(), "legacy-line",
                                        TextEncoding::Utf8);
    bad_extadr_scalar.origin.block          = block;
    bad_extadr_scalar.origin.order_in_block = 0;
    (void)store.add_entry(bad_extadr_scalar);

    Entry good_extadr1;
    good_extadr1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrExtadr[1]");
    good_extadr1.value = make_text(store.arena(), "Line 1",
                                   TextEncoding::Utf8);
    good_extadr1.origin.block          = block;
    good_extadr1.origin.order_in_block = 1;
    (void)store.add_entry(good_extadr1);

    Entry good_extadr2;
    good_extadr2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrExtadr[2]");
    good_extadr2.value = make_text(store.arena(), "Line 2",
                                   TextEncoding::Utf8);
    good_extadr2.origin.block          = block;
    good_extadr2.origin.order_in_block = 2;
    (void)store.add_entry(good_extadr2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:CiAdrExtadr>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Line 1</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Line 2</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-line"), std::string_view::npos);
}

TEST(XmpDump, PortablePreservesStructuredChildLangAltResources)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry email;
    email.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiEmailWork");
    email.value = make_text(store.arena(), "editor@example.test",
                            TextEncoding::Utf8);
    email.origin.block          = block;
    email.origin.order_in_block = 0;
    (void)store.add_entry(email);

    Entry city_default;
    city_default.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrCity[@xml:lang=x-default]");
    city_default.value = make_text(store.arena(), "Tokyo",
                                   TextEncoding::Utf8);
    city_default.origin.block          = block;
    city_default.origin.order_in_block = 1;
    (void)store.add_entry(city_default);

    Entry city_ja;
    city_ja.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrCity[@xml:lang=ja-JP]");
    city_ja.value = make_text(store.arena(), "東京", TextEncoding::Utf8);
    city_ja.origin.block          = block;
    city_ja.origin.order_in_block = 2;
    (void)store.add_entry(city_ja);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:CiAdrCity>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Alt>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"ja-JP\">東京</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePreservesIndexedStructuredChildLangAltResources)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry country;
    country.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/CountryName");
    country.value = make_text(store.arena(), "Japan", TextEncoding::Utf8);
    country.origin.block          = block;
    country.origin.order_in_block = 0;
    (void)store.add_entry(country);

    Entry sublocation_default;
    sublocation_default.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Sublocation[@xml:lang=x-default]");
    sublocation_default.value = make_text(store.arena(), "Gion",
                                          TextEncoding::Utf8);
    sublocation_default.origin.block          = block;
    sublocation_default.origin.order_in_block = 1;
    (void)store.add_entry(sublocation_default);

    Entry sublocation_ja;
    sublocation_ja.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Sublocation[@xml:lang=ja-JP]");
    sublocation_ja.value = make_text(store.arena(), "祇園",
                                     TextEncoding::Utf8);
    sublocation_ja.origin.block          = block;
    sublocation_ja.origin.order_in_block = 2;
    (void)store.add_entry(sublocation_ja);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li rdf:parseType=\"Resource\">"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:Sublocation>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Gion</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"ja-JP\">祇園</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePreservesStructuredChildIndexedResources)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry line1;
    line1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrExtadr[1]");
    line1.value = make_text(store.arena(), "Line 1", TextEncoding::Utf8);
    line1.origin.block          = block;
    line1.origin.order_in_block = 0;
    (void)store.add_entry(line1);

    Entry line2;
    line2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrExtadr[2]");
    line2.value = make_text(store.arena(), "Line 2", TextEncoding::Utf8);
    line2.origin.block          = block;
    line2.origin.order_in_block = 1;
    (void)store.add_entry(line2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:CiAdrExtadr>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Line 1</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Line 2</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortablePreservesIndexedStructuredChildIndexedResources)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry line1;
    line1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Sublocation[1]");
    line1.value = make_text(store.arena(), "Gion", TextEncoding::Utf8);
    line1.origin.block          = block;
    line1.origin.order_in_block = 0;
    (void)store.add_entry(line1);

    Entry line2;
    line2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Sublocation[2]");
    line2.value = make_text(store.arena(), "Hanamikoji", TextEncoding::Utf8);
    line2.origin.block          = block;
    line2.origin.order_in_block = 1;
    (void)store.add_entry(line2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li rdf:parseType=\"Resource\">"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:Sublocation>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Gion</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Hanamikoji</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortablePreservesNestedStructuredResources)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry province_name;
    province_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceName");
    province_name.value = make_text(store.arena(), "Tokyo",
                                    TextEncoding::Utf8);
    province_name.origin.block          = block;
    province_name.origin.order_in_block = 0;
    (void)store.add_entry(province_name);

    Entry province_code;
    province_code.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceCode");
    province_code.value = make_text(store.arena(), "13",
                                    TextEncoding::Utf8);
    province_code.origin.block          = block;
    province_code.origin.order_in_block = 1;
    (void)store.add_entry(province_code);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li>13</rdf:li>"),
        std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesIndexedNestedStructuredAddressAliases)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry city;
    city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/City");
    city.value = make_text(store.arena(), "Kyoto", TextEncoding::Utf8);
    city.origin.block          = block;
    city.origin.order_in_block = 0;
    (void)store.add_entry(city);

    Entry country;
    country.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/CountryName");
    country.value = make_text(store.arena(), "Japan", TextEncoding::Utf8);
    country.origin.block          = block;
    country.origin.order_in_block = 1;
    (void)store.add_entry(country);

    Entry country_code;
    country_code.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/CountryCode");
    country_code.value = make_text(store.arena(), "JP",
                                   TextEncoding::Utf8);
    country_code.origin.block          = block;
    country_code.origin.order_in_block = 2;
    (void)store.add_entry(country_code);

    Entry province;
    province.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/ProvinceState");
    province.value = make_text(store.arena(), "Kyoto",
                               TextEncoding::Utf8);
    province.origin.block          = block;
    province.origin.order_in_block = 3;
    (void)store.add_entry(province);

    Entry world_region;
    world_region.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/WorldRegion");
    world_region.value = make_text(store.arena(), "APAC",
                                   TextEncoding::Utf8);
    world_region.origin.block          = block;
    world_region.origin.order_in_block = 4;
    (void)store.add_entry(world_region);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li rdf:parseType=\"Resource\">"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:CountryCode>JP</Iptc4xmpExt:CountryCode>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:ProvinceState>Kyoto</Iptc4xmpExt:ProvinceState>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:WorldRegion>APAC</Iptc4xmpExt:WorldRegion>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find("<Iptc4xmpExt:Address rdf:parseType=\"Resource\">"),
        std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesStructuredLocationCreatedAddressAliases)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry city;
    city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/City");
    city.value = make_text(store.arena(), "Paris", TextEncoding::Utf8);
    city.origin.block          = block;
    city.origin.order_in_block = 0;
    (void)store.add_entry(city);

    Entry country_name;
    country_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/CountryName");
    country_name.value = make_text(store.arena(), "France",
                                   TextEncoding::Utf8);
    country_name.origin.block          = block;
    country_name.origin.order_in_block = 1;
    (void)store.add_entry(country_name);

    Entry country_code;
    country_code.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/CountryCode");
    country_code.value = make_text(store.arena(), "FR",
                                   TextEncoding::Utf8);
    country_code.origin.block          = block;
    country_code.origin.order_in_block = 2;
    (void)store.add_entry(country_code);

    Entry province;
    province.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/ProvinceState");
    province.value = make_text(store.arena(), "Ile-de-France",
                               TextEncoding::Utf8);
    province.origin.block          = block;
    province.origin.order_in_block = 3;
    (void)store.add_entry(province);

    Entry world_region;
    world_region.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/WorldRegion");
    world_region.value = make_text(store.arena(), "EMEA",
                                   TextEncoding::Utf8);
    world_region.origin.block          = block;
    world_region.origin.order_in_block = 4;
    (void)store.add_entry(world_region);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpExt:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:City>Paris</Iptc4xmpExt:City>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:CountryName>France</Iptc4xmpExt:CountryName>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:CountryCode>FR</Iptc4xmpExt:CountryCode>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:ProvinceState>Ile-de-France</Iptc4xmpExt:ProvinceState>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:WorldRegion>EMEA</Iptc4xmpExt:WorldRegion>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find("<Iptc4xmpExt:Address rdf:parseType=\"Resource\">"),
        std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesIndexedLocationDetailsChildShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry bad_name_scalar;
    bad_name_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationName");
    bad_name_scalar.value = make_text(store.arena(), "legacy-name",
                                      TextEncoding::Utf8);
    bad_name_scalar.origin.block          = block;
    bad_name_scalar.origin.order_in_block = 0;
    (void)store.add_entry(bad_name_scalar);

    Entry good_name_default;
    good_name_default.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationName[@xml:lang=x-default]");
    good_name_default.value = make_text(store.arena(), "Kyoto",
                                        TextEncoding::Utf8);
    good_name_default.origin.block          = block;
    good_name_default.origin.order_in_block = 1;
    (void)store.add_entry(good_name_default);

    Entry good_name_fr;
    good_name_fr.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationName[@xml:lang=fr-FR]");
    good_name_fr.value = make_text(store.arena(), "Kyoto FR",
                                   TextEncoding::Utf8);
    good_name_fr.origin.block          = block;
    good_name_fr.origin.order_in_block = 2;
    (void)store.add_entry(good_name_fr);

    Entry bad_id_scalar;
    bad_id_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationId");
    bad_id_scalar.value = make_text(store.arena(), "legacy-id",
                                    TextEncoding::Utf8);
    bad_id_scalar.origin.block          = block;
    bad_id_scalar.origin.order_in_block = 3;
    (void)store.add_entry(bad_id_scalar);

    Entry good_id1;
    good_id1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationId[1]");
    good_id1.value = make_text(store.arena(), "loc-001",
                               TextEncoding::Utf8);
    good_id1.origin.block          = block;
    good_id1.origin.order_in_block = 4;
    (void)store.add_entry(good_id1);

    Entry good_id2;
    good_id2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationId[2]");
    good_id2.value = make_text(store.arena(), "loc-002",
                               TextEncoding::Utf8);
    good_id2.origin.block          = block;
    good_id2.origin.order_in_block = 5;
    (void)store.add_entry(good_id2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationName>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Alt>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Kyoto</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"fr-FR\">Kyoto FR</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationId>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>loc-001</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>loc-002</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-name"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-id"), std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesStructuredLocationCreatedChildShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry bad_name_scalar;
    bad_name_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationName");
    bad_name_scalar.value = make_text(store.arena(), "legacy-name",
                                      TextEncoding::Utf8);
    bad_name_scalar.origin.block          = block;
    bad_name_scalar.origin.order_in_block = 0;
    (void)store.add_entry(bad_name_scalar);

    Entry good_name_default;
    good_name_default.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationName[@xml:lang=x-default]");
    good_name_default.value = make_text(store.arena(), "Paris",
                                        TextEncoding::Utf8);
    good_name_default.origin.block          = block;
    good_name_default.origin.order_in_block = 1;
    (void)store.add_entry(good_name_default);

    Entry good_name_fr;
    good_name_fr.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationName[@xml:lang=fr-FR]");
    good_name_fr.value = make_text(store.arena(), "Paris FR",
                                   TextEncoding::Utf8);
    good_name_fr.origin.block          = block;
    good_name_fr.origin.order_in_block = 2;
    (void)store.add_entry(good_name_fr);

    Entry bad_id_scalar;
    bad_id_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationId");
    bad_id_scalar.value = make_text(store.arena(), "legacy-id",
                                    TextEncoding::Utf8);
    bad_id_scalar.origin.block          = block;
    bad_id_scalar.origin.order_in_block = 3;
    (void)store.add_entry(bad_id_scalar);

    Entry good_id1;
    good_id1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationId[1]");
    good_id1.value = make_text(store.arena(), "paris-001",
                               TextEncoding::Utf8);
    good_id1.origin.block          = block;
    good_id1.origin.order_in_block = 4;
    (void)store.add_entry(good_id1);

    Entry good_id2;
    good_id2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationId[2]");
    good_id2.value = make_text(store.arena(), "paris-002",
                               TextEncoding::Utf8);
    good_id2.origin.block          = block;
    good_id2.origin.order_in_block = 5;
    (void)store.add_entry(good_id2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpExt:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationName>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Alt>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Paris</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"fr-FR\">Paris FR</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationId>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>paris-001</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>paris-002</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-name"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-id"), std::string_view::npos);
}

TEST(XmpDump, PortablePreservesMixedNamespaceStructuredLocationDetailsChildren)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry id1;
    id1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/xmp:Identifier[1]");
    id1.value = make_text(store.arena(), "loc-001", TextEncoding::Utf8);
    id1.origin.block          = block;
    id1.origin.order_in_block = 0;
    (void)store.add_entry(id1);

    Entry id2;
    id2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/xmp:Identifier[2]");
    id2.value = make_text(store.arena(), "loc-002", TextEncoding::Utf8);
    id2.origin.block          = block;
    id2.origin.order_in_block = 1;
    (void)store.add_entry(id2);

    Entry gps_lat;
    gps_lat.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/exif:GPSLatitude");
    gps_lat.value = make_text(store.arena(), "41,24.5N",
                              TextEncoding::Utf8);
    gps_lat.origin.block          = block;
    gps_lat.origin.order_in_block = 2;
    (void)store.add_entry(gps_lat);

    Entry gps_lon;
    gps_lon.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/exif:GPSLongitude");
    gps_lon.value = make_text(store.arena(), "2,9E", TextEncoding::Utf8);
    gps_lon.origin.block          = block;
    gps_lon.origin.order_in_block = 3;
    (void)store.add_entry(gps_lon);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(s.find("<xmp:Identifier>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>loc-001</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>loc-002</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSLatitude>41,24.5N</exif:GPSLatitude>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSLongitude>2,9E</exif:GPSLongitude>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePromotesLegacyMixedNamespaceLocationDetailsChildren)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry shown_id1;
    shown_id1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Identifier[1]");
    shown_id1.value = make_text(store.arena(), "loc-001",
                                TextEncoding::Utf8);
    shown_id1.origin.block          = block;
    shown_id1.origin.order_in_block = 0;
    (void)store.add_entry(shown_id1);

    Entry shown_id2;
    shown_id2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Identifier[2]");
    shown_id2.value = make_text(store.arena(), "loc-002",
                                TextEncoding::Utf8);
    shown_id2.origin.block          = block;
    shown_id2.origin.order_in_block = 1;
    (void)store.add_entry(shown_id2);

    Entry shown_gps_lat;
    shown_gps_lat.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/GPSLatitude");
    shown_gps_lat.value = make_text(store.arena(), "35,40.123N",
                                    TextEncoding::Utf8);
    shown_gps_lat.origin.block          = block;
    shown_gps_lat.origin.order_in_block = 2;
    (void)store.add_entry(shown_gps_lat);

    Entry shown_gps_lon;
    shown_gps_lon.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/GPSLongitude");
    shown_gps_lon.value = make_text(store.arena(), "139,42.456E",
                                    TextEncoding::Utf8);
    shown_gps_lon.origin.block          = block;
    shown_gps_lon.origin.order_in_block = 3;
    (void)store.add_entry(shown_gps_lon);

    Entry shown_gps_alt;
    shown_gps_alt.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/GPSAltitude");
    shown_gps_alt.value = make_text(store.arena(), "35.5",
                                    TextEncoding::Utf8);
    shown_gps_alt.origin.block          = block;
    shown_gps_alt.origin.order_in_block = 4;
    (void)store.add_entry(shown_gps_alt);

    Entry shown_gps_alt_ref;
    shown_gps_alt_ref.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/GPSAltitudeRef");
    shown_gps_alt_ref.value = make_text(store.arena(), "Above Sea Level",
                                        TextEncoding::Utf8);
    shown_gps_alt_ref.origin.block          = block;
    shown_gps_alt_ref.origin.order_in_block = 5;
    (void)store.add_entry(shown_gps_alt_ref);

    Entry created_id1;
    created_id1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Identifier[1]");
    created_id1.value = make_text(store.arena(), "par-001",
                                  TextEncoding::Utf8);
    created_id1.origin.block          = block;
    created_id1.origin.order_in_block = 6;
    (void)store.add_entry(created_id1);

    Entry created_gps_lat;
    created_gps_lat.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/GPSLatitude");
    created_gps_lat.value = make_text(store.arena(), "48,51.507N",
                                      TextEncoding::Utf8);
    created_gps_lat.origin.block          = block;
    created_gps_lat.origin.order_in_block = 7;
    (void)store.add_entry(created_gps_lat);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(s.find("<xmp:Identifier>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>loc-001</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>loc-002</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSLatitude>35,40.123N</exif:GPSLatitude>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<exif:GPSLongitude>139,42.456E</exif:GPSLongitude>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSAltitude>35.5</exif:GPSAltitude>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<exif:GPSAltitudeRef>Above Sea Level</exif:GPSAltitudeRef>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>par-001</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSLatitude>48,51.507N</exif:GPSLatitude>"),
              std::string_view::npos);

    EXPECT_EQ(s.find("<Iptc4xmpExt:Identifier>"), std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:GPSLatitude>"), std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:GPSLongitude>"), std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:GPSAltitude>"), std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:GPSAltitudeRef>"),
              std::string_view::npos);
}

TEST(XmpDump,
     PortableDoesNotDuplicateLegacyMixedNamespaceIndexedPromotion)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry shown_identifier_scalar;
    shown_identifier_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Identifier");
    shown_identifier_scalar.value = make_text(store.arena(), "legacy-id-scalar",
                                              TextEncoding::Utf8);
    shown_identifier_scalar.origin.block          = block;
    shown_identifier_scalar.origin.order_in_block = 0;
    (void)store.add_entry(shown_identifier_scalar);

    Entry shown_identifier_indexed;
    shown_identifier_indexed.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Identifier[1]");
    shown_identifier_indexed.value = make_text(store.arena(), "loc-001",
                                               TextEncoding::Utf8);
    shown_identifier_indexed.origin.block          = block;
    shown_identifier_indexed.origin.order_in_block = 1;
    (void)store.add_entry(shown_identifier_indexed);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<xmp:Identifier>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>loc-001</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li>legacy-id-scalar</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump,
     PortableDoesNotDuplicateLegacyAdobeNestedQualifiedScalarPromotion)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry legacy_file_path;
    legacy_file_path.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Manifest[1]/reference/filePath");
    legacy_file_path.value = make_text(store.arena(),
                                       "/tmp/legacy-manifest.dat",
                                       TextEncoding::Utf8);
    legacy_file_path.origin.block          = block;
    legacy_file_path.origin.order_in_block = 0;
    (void)store.add_entry(legacy_file_path);

    Entry canonical_file_path;
    canonical_file_path.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Manifest[1]/stMfs:reference/stRef:filePath");
    canonical_file_path.value = make_text(store.arena(),
                                          "/tmp/canonical-manifest.dat",
                                          TextEncoding::Utf8);
    canonical_file_path.origin.block          = block;
    canonical_file_path.origin.order_in_block = 1;
    (void)store.add_entry(canonical_file_path);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<xmpMM:Manifest>"), std::string_view::npos);
    EXPECT_NE(s.find("<stMfs:reference"), std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:filePath>/tmp/canonical-manifest.dat</stRef:filePath>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find("<stRef:filePath>/tmp/legacy-manifest.dat</stRef:filePath>"),
        std::string_view::npos);
}

TEST(XmpDump,
     PortableDoesNotDuplicateLegacyAdobeQualifiedStructuredFamilies)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry legacy_rendition_manage_to;
    legacy_rendition_manage_to.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "RenditionOf/manageTo");
    legacy_rendition_manage_to.value = make_text(
        store.arena(), "https://example.invalid/legacy-rendition",
        TextEncoding::Utf8);
    legacy_rendition_manage_to.origin.block          = block;
    legacy_rendition_manage_to.origin.order_in_block = 0;
    (void)store.add_entry(legacy_rendition_manage_to);

    Entry canonical_rendition_manage_to;
    canonical_rendition_manage_to.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "RenditionOf/stRef:manageTo");
    canonical_rendition_manage_to.value = make_text(
        store.arena(), "https://example.invalid/canonical-rendition",
        TextEncoding::Utf8);
    canonical_rendition_manage_to.origin.block          = block;
    canonical_rendition_manage_to.origin.order_in_block = 1;
    (void)store.add_entry(canonical_rendition_manage_to);

    Entry legacy_history_agent;
    legacy_history_agent.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "History[1]/softwareAgent");
    legacy_history_agent.value = make_text(store.arena(),
                                           "LegacyAgent",
                                           TextEncoding::Utf8);
    legacy_history_agent.origin.block          = block;
    legacy_history_agent.origin.order_in_block = 2;
    (void)store.add_entry(legacy_history_agent);

    Entry canonical_history_agent;
    canonical_history_agent.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "History[1]/stEvt:softwareAgent");
    canonical_history_agent.value = make_text(store.arena(),
                                              "CanonicalAgent",
                                              TextEncoding::Utf8);
    canonical_history_agent.origin.block          = block;
    canonical_history_agent.origin.order_in_block = 3;
    (void)store.add_entry(canonical_history_agent);

    Entry legacy_versions_action;
    legacy_versions_action.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/event/action");
    legacy_versions_action.value = make_text(store.arena(),
                                             "legacy-saved",
                                             TextEncoding::Utf8);
    legacy_versions_action.origin.block          = block;
    legacy_versions_action.origin.order_in_block = 4;
    (void)store.add_entry(legacy_versions_action);

    Entry canonical_versions_action;
    canonical_versions_action.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/stVer:event/stEvt:action");
    canonical_versions_action.value = make_text(store.arena(),
                                                "saved",
                                                TextEncoding::Utf8);
    canonical_versions_action.origin.block          = block;
    canonical_versions_action.origin.order_in_block = 5;
    (void)store.add_entry(canonical_versions_action);

    Entry legacy_video_width;
    legacy_video_width.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoFrameSize/w");
    legacy_video_width.value = make_text(store.arena(), "1280",
                                         TextEncoding::Utf8);
    legacy_video_width.origin.block          = block;
    legacy_video_width.origin.order_in_block = 6;
    (void)store.add_entry(legacy_video_width);

    Entry canonical_video_width;
    canonical_video_width.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoFrameSize/stDim:w");
    canonical_video_width.value = make_text(store.arena(), "1920",
                                            TextEncoding::Utf8);
    canonical_video_width.origin.block          = block;
    canonical_video_width.origin.order_in_block = 7;
    (void)store.add_entry(canonical_video_width);

    Entry legacy_colorant_name;
    legacy_colorant_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups[1]/Colorants[1]/swatchName");
    legacy_colorant_name.value = make_text(store.arena(), "Legacy Orange",
                                           TextEncoding::Utf8);
    legacy_colorant_name.origin.block          = block;
    legacy_colorant_name.origin.order_in_block = 8;
    (void)store.add_entry(legacy_colorant_name);

    Entry canonical_colorant_name;
    canonical_colorant_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups[1]/Colorants[1]/xmpG:swatchName");
    canonical_colorant_name.value = make_text(store.arena(),
                                              "Accent Orange",
                                              TextEncoding::Utf8);
    canonical_colorant_name.origin.block          = block;
    canonical_colorant_name.origin.order_in_block = 9;
    (void)store.add_entry(canonical_colorant_name);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find(
            "<stRef:manageTo>https://example.invalid/canonical-rendition</stRef:manageTo>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find(
            "<stRef:manageTo>https://example.invalid/legacy-rendition</stRef:manageTo>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<stEvt:softwareAgent>CanonicalAgent</stEvt:softwareAgent>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<stEvt:softwareAgent>LegacyAgent</stEvt:softwareAgent>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<stEvt:action>saved</stEvt:action>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<stEvt:action>legacy-saved</stEvt:action>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<stDim:w>1920</stDim:w>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<stDim:w>1280</stDim:w>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpG:swatchName>Accent Orange</xmpG:swatchName>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpG:swatchName>Legacy Orange</xmpG:swatchName>"),
              std::string_view::npos);
}

TEST(XmpDump,
     PortableDoesNotDuplicateAdditionalLegacyAdobeQualifiedStructuredFamilies)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry legacy_document_id;
    legacy_document_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "DerivedFrom/documentID");
    legacy_document_id.value = make_text(store.arena(),
                                         "xmp.did:legacy-base",
                                         TextEncoding::Utf8);
    legacy_document_id.origin.block          = block;
    legacy_document_id.origin.order_in_block = 0;
    (void)store.add_entry(legacy_document_id);

    Entry canonical_document_id;
    canonical_document_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "DerivedFrom/stRef:documentID");
    canonical_document_id.value = make_text(store.arena(),
                                            "xmp.did:canonical-base",
                                            TextEncoding::Utf8);
    canonical_document_id.origin.block          = block;
    canonical_document_id.origin.order_in_block = 1;
    (void)store.add_entry(canonical_document_id);

    Entry legacy_manage_ui;
    legacy_manage_ui.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Manifest[1]/reference/manageUI");
    legacy_manage_ui.value = make_text(
        store.arena(), "https://example.invalid/legacy-manage",
        TextEncoding::Utf8);
    legacy_manage_ui.origin.block          = block;
    legacy_manage_ui.origin.order_in_block = 2;
    (void)store.add_entry(legacy_manage_ui);

    Entry canonical_manage_ui;
    canonical_manage_ui.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Manifest[1]/stMfs:reference/stRef:manageUI");
    canonical_manage_ui.value = make_text(
        store.arena(), "https://example.invalid/canonical-manage",
        TextEncoding::Utf8);
    canonical_manage_ui.origin.block          = block;
    canonical_manage_ui.origin.order_in_block = 3;
    (void)store.add_entry(canonical_manage_ui);

    Entry legacy_child_font_file;
    legacy_child_font_file.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "Fonts[1]/childFontFiles[1]");
    legacy_child_font_file.value = make_text(store.arena(),
                                             "LegacyFont.otf",
                                             TextEncoding::Utf8);
    legacy_child_font_file.origin.block          = block;
    legacy_child_font_file.origin.order_in_block = 4;
    (void)store.add_entry(legacy_child_font_file);

    Entry canonical_child_font_file;
    canonical_child_font_file.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "Fonts[1]/stFnt:childFontFiles[1]");
    canonical_child_font_file.value = make_text(
        store.arena(), "SourceSerif-Regular.otf",
        TextEncoding::Utf8);
    canonical_child_font_file.origin.block          = block;
    canonical_child_font_file.origin.order_in_block = 5;
    (void)store.add_entry(canonical_child_font_file);

    Entry legacy_max_page_w;
    legacy_max_page_w.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "MaxPageSize/w");
    legacy_max_page_w.value = make_text(store.arena(), "8.5",
                                        TextEncoding::Utf8);
    legacy_max_page_w.origin.block          = block;
    legacy_max_page_w.origin.order_in_block = 6;
    (void)store.add_entry(legacy_max_page_w);

    Entry canonical_max_page_w;
    canonical_max_page_w.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "MaxPageSize/stDim:w");
    canonical_max_page_w.value = make_text(store.arena(), "8.75",
                                           TextEncoding::Utf8);
    canonical_max_page_w.origin.block          = block;
    canonical_max_page_w.origin.order_in_block = 7;
    (void)store.add_entry(canonical_max_page_w);

    Entry legacy_managed_from_instance_id;
    legacy_managed_from_instance_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "ManagedFrom/instanceID");
    legacy_managed_from_instance_id.value = make_text(
        store.arena(), "xmp.iid:legacy-managed",
        TextEncoding::Utf8);
    legacy_managed_from_instance_id.origin.block          = block;
    legacy_managed_from_instance_id.origin.order_in_block = 8;
    (void)store.add_entry(legacy_managed_from_instance_id);

    Entry canonical_managed_from_instance_id;
    canonical_managed_from_instance_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "ManagedFrom/stRef:instanceID");
    canonical_managed_from_instance_id.value = make_text(
        store.arena(), "xmp.iid:canonical-managed",
        TextEncoding::Utf8);
    canonical_managed_from_instance_id.origin.block          = block;
    canonical_managed_from_instance_id.origin.order_in_block = 9;
    (void)store.add_entry(canonical_managed_from_instance_id);

    Entry legacy_history_parameters;
    legacy_history_parameters.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "History[1]/parameters");
    legacy_history_parameters.value = make_text(
        store.arena(), "legacy-history-params",
        TextEncoding::Utf8);
    legacy_history_parameters.origin.block          = block;
    legacy_history_parameters.origin.order_in_block = 10;
    (void)store.add_entry(legacy_history_parameters);

    Entry canonical_history_parameters;
    canonical_history_parameters.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "History[1]/stEvt:parameters");
    canonical_history_parameters.value = make_text(
        store.arena(), "canonical-history-params",
        TextEncoding::Utf8);
    canonical_history_parameters.origin.block          = block;
    canonical_history_parameters.origin.order_in_block = 11;
    (void)store.add_entry(canonical_history_parameters);

    Entry legacy_font_name;
    legacy_font_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "Fonts[1]/fontName");
    legacy_font_name.value = make_text(store.arena(), "Legacy Display",
                                       TextEncoding::Utf8);
    legacy_font_name.origin.block          = block;
    legacy_font_name.origin.order_in_block = 12;
    (void)store.add_entry(legacy_font_name);

    Entry canonical_font_name;
    canonical_font_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "Fonts[1]/stFnt:fontName");
    canonical_font_name.value = make_text(
        store.arena(), "Source Serif Display",
        TextEncoding::Utf8);
    canonical_font_name.origin.block          = block;
    canonical_font_name.origin.order_in_block = 13;
    (void)store.add_entry(canonical_font_name);

    Entry legacy_colorant_mode;
    legacy_colorant_mode.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups[1]/Colorants[1]/mode");
    legacy_colorant_mode.value = make_text(store.arena(), "CMYK",
                                           TextEncoding::Utf8);
    legacy_colorant_mode.origin.block          = block;
    legacy_colorant_mode.origin.order_in_block = 14;
    (void)store.add_entry(legacy_colorant_mode);

    Entry canonical_colorant_mode;
    canonical_colorant_mode.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/t/pg/",
        "SwatchGroups[1]/Colorants[1]/xmpG:mode");
    canonical_colorant_mode.value = make_text(store.arena(), "RGB",
                                              TextEncoding::Utf8);
    canonical_colorant_mode.origin.block          = block;
    canonical_colorant_mode.origin.order_in_block = 15;
    (void)store.add_entry(canonical_colorant_mode);

    Entry legacy_job_id;
    legacy_job_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/bj/",
        "JobRef[1]/id");
    legacy_job_id.value = make_text(store.arena(), "legacy-job-id",
                                    TextEncoding::Utf8);
    legacy_job_id.origin.block          = block;
    legacy_job_id.origin.order_in_block = 16;
    (void)store.add_entry(legacy_job_id);

    Entry canonical_job_id;
    canonical_job_id.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/bj/",
        "JobRef[1]/stJob:id");
    canonical_job_id.value = make_text(store.arena(), "canonical-job-id",
                                       TextEncoding::Utf8);
    canonical_job_id.origin.block          = block;
    canonical_job_id.origin.order_in_block = 17;
    (void)store.add_entry(canonical_job_id);

    Entry legacy_job_url;
    legacy_job_url.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/bj/",
        "JobRef[1]/url");
    legacy_job_url.value = make_text(
        store.arena(), "https://example.invalid/legacy-job",
        TextEncoding::Utf8);
    legacy_job_url.origin.block          = block;
    legacy_job_url.origin.order_in_block = 18;
    (void)store.add_entry(legacy_job_url);

    Entry canonical_job_url;
    canonical_job_url.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/bj/",
        "JobRef[1]/stJob:url");
    canonical_job_url.value = make_text(
        store.arena(), "https://example.invalid/canonical-job",
        TextEncoding::Utf8);
    canonical_job_url.origin.block          = block;
    canonical_job_url.origin.order_in_block = 19;
    (void)store.add_entry(canonical_job_url);

    Entry legacy_rendition_part_mapping;
    legacy_rendition_part_mapping.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "RenditionOf/partMapping");
    legacy_rendition_part_mapping.value = make_text(
        store.arena(), "legacy-part-map", TextEncoding::Utf8);
    legacy_rendition_part_mapping.origin.block          = block;
    legacy_rendition_part_mapping.origin.order_in_block = 20;
    (void)store.add_entry(legacy_rendition_part_mapping);

    Entry canonical_rendition_part_mapping;
    canonical_rendition_part_mapping.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "RenditionOf/stRef:partMapping");
    canonical_rendition_part_mapping.value = make_text(
        store.arena(), "canonical-part-map", TextEncoding::Utf8);
    canonical_rendition_part_mapping.origin.block          = block;
    canonical_rendition_part_mapping.origin.order_in_block = 21;
    (void)store.add_entry(canonical_rendition_part_mapping);

    Entry legacy_versions_when;
    legacy_versions_when.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/event/when");
    legacy_versions_when.value = make_text(
        store.arena(), "2026-04-18T11:22:33Z", TextEncoding::Utf8);
    legacy_versions_when.origin.block          = block;
    legacy_versions_when.origin.order_in_block = 22;
    (void)store.add_entry(legacy_versions_when);

    Entry canonical_versions_when;
    canonical_versions_when.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/",
        "Versions[1]/stVer:event/stEvt:when");
    canonical_versions_when.value = make_text(
        store.arena(), "2026-04-19T01:02:03Z", TextEncoding::Utf8);
    canonical_versions_when.origin.block          = block;
    canonical_versions_when.origin.order_in_block = 23;
    (void)store.add_entry(canonical_versions_when);

    Entry legacy_frame_unit;
    legacy_frame_unit.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoFrameSize/unit");
    legacy_frame_unit.value = make_text(store.arena(), "inch",
                                        TextEncoding::Utf8);
    legacy_frame_unit.origin.block          = block;
    legacy_frame_unit.origin.order_in_block = 24;
    (void)store.add_entry(legacy_frame_unit);

    Entry canonical_frame_unit;
    canonical_frame_unit.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoFrameSize/stDim:unit");
    canonical_frame_unit.value = make_text(store.arena(), "pixel",
                                           TextEncoding::Utf8);
    canonical_frame_unit.origin.block          = block;
    canonical_frame_unit.origin.order_in_block = 25;
    (void)store.add_entry(canonical_frame_unit);

    Entry legacy_alpha_red;
    legacy_alpha_red.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoAlphaPremultipleColor/red");
    legacy_alpha_red.value = make_text(store.arena(), "32",
                                       TextEncoding::Utf8);
    legacy_alpha_red.origin.block          = block;
    legacy_alpha_red.origin.order_in_block = 26;
    (void)store.add_entry(legacy_alpha_red);

    Entry canonical_alpha_red;
    canonical_alpha_red.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xmp/1.0/DynamicMedia/",
        "videoAlphaPremultipleColor/xmpG:red");
    canonical_alpha_red.value = make_text(store.arena(), "255",
                                          TextEncoding::Utf8);
    canonical_alpha_red.origin.block          = block;
    canonical_alpha_red.origin.order_in_block = 27;
    (void)store.add_entry(canonical_alpha_red);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<stRef:documentID>xmp.did:canonical-base</stRef:documentID>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find("<stRef:documentID>xmp.did:legacy-base</stRef:documentID>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<stRef:manageUI>https://example.invalid/canonical-manage</stRef:manageUI>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find(
            "<stRef:manageUI>https://example.invalid/legacy-manage</stRef:manageUI>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<stFnt:childFontFiles>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>SourceSerif-Regular.otf</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li>LegacyFont.otf</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<stDim:w>8.75</stDim:w>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<stDim:w>8.5</stDim:w>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<stRef:instanceID>xmp.iid:canonical-managed</stRef:instanceID>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find("<stRef:instanceID>xmp.iid:legacy-managed</stRef:instanceID>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stEvt:parameters>canonical-history-params</stEvt:parameters>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find("<stEvt:parameters>legacy-history-params</stEvt:parameters>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<stFnt:fontName>Source Serif Display</stFnt:fontName>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("<stFnt:fontName>Legacy Display</stFnt:fontName>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpG:mode>RGB</xmpG:mode>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpG:mode>CMYK</xmpG:mode>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<stJob:id>canonical-job-id</stJob:id>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<stJob:id>legacy-job-id</stJob:id>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<stJob:url>https://example.invalid/canonical-job</stJob:url>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find("<stJob:url>https://example.invalid/legacy-job</stJob:url>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<stRef:partMapping>canonical-part-map</stRef:partMapping>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<stRef:partMapping>legacy-part-map</stRef:partMapping>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<stEvt:when>2026-04-19T01:02:03Z</stEvt:when>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<stEvt:when>2026-04-18T11:22:33Z</stEvt:when>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<stDim:unit>pixel</stDim:unit>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<stDim:unit>inch</stDim:unit>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpG:red>255</xmpG:red>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpG:red>32</xmpG:red>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesIptcExtEntityWithRoleShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry bad_creator_scalar;
    bad_creator_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator");
    bad_creator_scalar.value = make_text(store.arena(), "legacy-creator",
                                         TextEncoding::Utf8);
    bad_creator_scalar.origin.block          = block;
    bad_creator_scalar.origin.order_in_block = 0;
    (void)store.add_entry(bad_creator_scalar);

    Entry bad_name_scalar;
    bad_name_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Name");
    bad_name_scalar.value = make_text(store.arena(), "legacy-name",
                                      TextEncoding::Utf8);
    bad_name_scalar.origin.block          = block;
    bad_name_scalar.origin.order_in_block = 1;
    (void)store.add_entry(bad_name_scalar);

    Entry good_name_default;
    good_name_default.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Name[@xml:lang=x-default]");
    good_name_default.value = make_text(store.arena(), "Alice Example",
                                        TextEncoding::Utf8);
    good_name_default.origin.block          = block;
    good_name_default.origin.order_in_block = 2;
    (void)store.add_entry(good_name_default);

    Entry good_name_ja;
    good_name_ja.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Name[@xml:lang=ja-JP]");
    good_name_ja.value = make_text(store.arena(), "アリス", TextEncoding::Utf8);
    good_name_ja.origin.block          = block;
    good_name_ja.origin.order_in_block = 3;
    (void)store.add_entry(good_name_ja);

    Entry bad_role_scalar;
    bad_role_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Role");
    bad_role_scalar.value = make_text(store.arena(), "legacy-role",
                                      TextEncoding::Utf8);
    bad_role_scalar.origin.block          = block;
    bad_role_scalar.origin.order_in_block = 4;
    (void)store.add_entry(bad_role_scalar);

    Entry good_role1;
    good_role1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Role[1]");
    good_role1.value = make_text(store.arena(), "photographer",
                                 TextEncoding::Utf8);
    good_role1.origin.block          = block;
    good_role1.origin.order_in_block = 5;
    (void)store.add_entry(good_role1);

    Entry good_role2;
    good_role2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Role[2]");
    good_role2.value = make_text(store.arena(), "editor",
                                 TextEncoding::Utf8);
    good_role2.origin.block          = block;
    good_role2.origin.order_in_block = 6;
    (void)store.add_entry(good_role2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:Creator>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:Name>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Alice Example</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"ja-JP\">アリス</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:Role>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>photographer</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>editor</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-creator"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-name"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-role"), std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesArtworkOrObjectShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry bad_artwork_scalar;
    bad_artwork_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject");
    bad_artwork_scalar.value = make_text(store.arena(), "legacy-artwork",
                                         TextEncoding::Utf8);
    bad_artwork_scalar.origin.block          = block;
    bad_artwork_scalar.origin.order_in_block = 0;
    (void)store.add_entry(bad_artwork_scalar);

    Entry bad_title_scalar;
    bad_title_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOTitle");
    bad_title_scalar.value = make_text(store.arena(), "legacy-title",
                                       TextEncoding::Utf8);
    bad_title_scalar.origin.block          = block;
    bad_title_scalar.origin.order_in_block = 1;
    (void)store.add_entry(bad_title_scalar);

    Entry good_title_default;
    good_title_default.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOTitle[@xml:lang=x-default]");
    good_title_default.value = make_text(store.arena(), "Sunset Study",
                                         TextEncoding::Utf8);
    good_title_default.origin.block          = block;
    good_title_default.origin.order_in_block = 2;
    (void)store.add_entry(good_title_default);

    Entry bad_creator_scalar;
    bad_creator_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOCreator");
    bad_creator_scalar.value = make_text(store.arena(), "legacy-creator",
                                         TextEncoding::Utf8);
    bad_creator_scalar.origin.block          = block;
    bad_creator_scalar.origin.order_in_block = 3;
    (void)store.add_entry(bad_creator_scalar);

    Entry good_creator1;
    good_creator1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOCreator[1]");
    good_creator1.value = make_text(store.arena(), "Alice Example",
                                    TextEncoding::Utf8);
    good_creator1.origin.block          = block;
    good_creator1.origin.order_in_block = 4;
    (void)store.add_entry(good_creator1);

    Entry good_creator2;
    good_creator2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOCreator[2]");
    good_creator2.value = make_text(store.arena(), "Bob Example",
                                    TextEncoding::Utf8);
    good_creator2.origin.block          = block;
    good_creator2.origin.order_in_block = 5;
    (void)store.add_entry(good_creator2);

    Entry bad_style_scalar;
    bad_style_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOStylePeriod");
    bad_style_scalar.value = make_text(store.arena(), "legacy-style",
                                       TextEncoding::Utf8);
    bad_style_scalar.origin.block          = block;
    bad_style_scalar.origin.order_in_block = 6;
    (void)store.add_entry(bad_style_scalar);

    Entry good_style1;
    good_style1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOStylePeriod[1]");
    good_style1.value = make_text(store.arena(), "Impressionism",
                                  TextEncoding::Utf8);
    good_style1.origin.block          = block;
    good_style1.origin.order_in_block = 7;
    (void)store.add_entry(good_style1);

    Entry good_style2;
    good_style2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOStylePeriod[2]");
    good_style2.value = make_text(store.arena(), "Modernism",
                                  TextEncoding::Utf8);
    good_style2.origin.block          = block;
    good_style2.origin.order_in_block = 8;
    (void)store.add_entry(good_style2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:ArtworkOrObject>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:AOTitle>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Sunset Study</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:AOCreator>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Alice Example</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Bob Example</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:AOStylePeriod>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Impressionism</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Modernism</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("legacy-artwork"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-title"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-creator"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-style"), std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesPersonAndProductDetailShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry bad_person_scalar;
    bad_person_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails");
    bad_person_scalar.value = make_text(store.arena(), "legacy-person",
                                        TextEncoding::Utf8);
    bad_person_scalar.origin.block          = block;
    bad_person_scalar.origin.order_in_block = 0;
    (void)store.add_entry(bad_person_scalar);

    Entry bad_person_name;
    bad_person_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonName");
    bad_person_name.value = make_text(store.arena(), "legacy-person-name",
                                      TextEncoding::Utf8);
    bad_person_name.origin.block          = block;
    bad_person_name.origin.order_in_block = 1;
    (void)store.add_entry(bad_person_name);

    Entry good_person_name_default;
    good_person_name_default.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonName[@xml:lang=x-default]");
    good_person_name_default.value = make_text(store.arena(), "Jane Doe",
                                               TextEncoding::Utf8);
    good_person_name_default.origin.block          = block;
    good_person_name_default.origin.order_in_block = 2;
    (void)store.add_entry(good_person_name_default);

    Entry bad_person_id;
    bad_person_id.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonId");
    bad_person_id.value = make_text(store.arena(), "legacy-person-id",
                                    TextEncoding::Utf8);
    bad_person_id.origin.block          = block;
    bad_person_id.origin.order_in_block = 3;
    (void)store.add_entry(bad_person_id);

    Entry good_person_id1;
    good_person_id1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonId[1]");
    good_person_id1.value = make_text(store.arena(), "person-001",
                                      TextEncoding::Utf8);
    good_person_id1.origin.block          = block;
    good_person_id1.origin.order_in_block = 4;
    (void)store.add_entry(good_person_id1);

    Entry good_person_id2;
    good_person_id2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonId[2]");
    good_person_id2.value = make_text(store.arena(), "person-002",
                                      TextEncoding::Utf8);
    good_person_id2.origin.block          = block;
    good_person_id2.origin.order_in_block = 5;
    (void)store.add_entry(good_person_id2);

    Entry bad_product_scalar;
    bad_product_scalar.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ProductInImage");
    bad_product_scalar.value = make_text(store.arena(), "legacy-product",
                                         TextEncoding::Utf8);
    bad_product_scalar.origin.block          = block;
    bad_product_scalar.origin.order_in_block = 6;
    (void)store.add_entry(bad_product_scalar);

    Entry bad_product_name;
    bad_product_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ProductInImage[1]/ProductName");
    bad_product_name.value = make_text(store.arena(), "legacy-product-name",
                                       TextEncoding::Utf8);
    bad_product_name.origin.block          = block;
    bad_product_name.origin.order_in_block = 7;
    (void)store.add_entry(bad_product_name);

    Entry good_product_name_default;
    good_product_name_default.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ProductInImage[1]/ProductName[@xml:lang=x-default]");
    good_product_name_default.value = make_text(store.arena(), "Camera Body",
                                                TextEncoding::Utf8);
    good_product_name_default.origin.block          = block;
    good_product_name_default.origin.order_in_block = 8;
    (void)store.add_entry(good_product_name_default);

    Entry bad_product_desc;
    bad_product_desc.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ProductInImage[1]/ProductDescription");
    bad_product_desc.value = make_text(store.arena(), "legacy-product-desc",
                                       TextEncoding::Utf8);
    bad_product_desc.origin.block          = block;
    bad_product_desc.origin.order_in_block = 9;
    (void)store.add_entry(bad_product_desc);

    Entry good_product_desc_default;
    good_product_desc_default.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ProductInImage[1]/ProductDescription[@xml:lang=x-default]");
    good_product_desc_default.value = make_text(store.arena(), "Mirrorless",
                                                TextEncoding::Utf8);
    good_product_desc_default.origin.block          = block;
    good_product_desc_default.origin.order_in_block = 10;
    (void)store.add_entry(good_product_desc_default);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:PersonInImageWDetails>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:PersonName>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Jane Doe</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:PersonId>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>person-001</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>person-002</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:ProductInImage>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:ProductName>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Camera Body</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Mirrorless</rdf:li>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("legacy-person"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-person-name"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-person-id"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-product"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-product-name"), std::string_view::npos);
    EXPECT_EQ(s.find("legacy-product-desc"), std::string_view::npos);
}

TEST(XmpDump, PortablePromotesFlatStructuredChildScalarsToCanonicalShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry creator_name;
    creator_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Name");
    creator_name.value = make_text(store.arena(), "Alice Flat",
                                   TextEncoding::Utf8);
    creator_name.origin.block          = block;
    creator_name.origin.order_in_block = 0;
    (void)store.add_entry(creator_name);

    Entry creator_role;
    creator_role.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Role");
    creator_role.value = make_text(store.arena(), "photographer",
                                   TextEncoding::Utf8);
    creator_role.origin.block          = block;
    creator_role.origin.order_in_block = 1;
    (void)store.add_entry(creator_role);

    Entry artwork_creator;
    artwork_creator.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOCreator");
    artwork_creator.value = make_text(store.arena(), "Alice Example",
                                      TextEncoding::Utf8);
    artwork_creator.origin.block          = block;
    artwork_creator.origin.order_in_block = 2;
    (void)store.add_entry(artwork_creator);

    Entry location_name;
    location_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationName");
    location_name.value = make_text(store.arena(), "City Hall",
                                    TextEncoding::Utf8);
    location_name.origin.block          = block;
    location_name.origin.order_in_block = 3;
    (void)store.add_entry(location_name);

    Entry location_id;
    location_id.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationId");
    location_id.value = make_text(store.arena(), "loc-001",
                                  TextEncoding::Utf8);
    location_id.origin.block          = block;
    location_id.origin.order_in_block = 4;
    (void)store.add_entry(location_id);

    Entry person_id;
    person_id.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonId");
    person_id.value = make_text(store.arena(), "person-001",
                                TextEncoding::Utf8);
    person_id.origin.block          = block;
    person_id.origin.order_in_block = 5;
    (void)store.add_entry(person_id);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Alice Flat</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:Role>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>photographer</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:AOCreator>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Alice Example</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">City Hall</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationId>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>loc-001</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:PersonId>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>person-001</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:LocationName>City Hall</Iptc4xmpExt:LocationName>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:Role>photographer</Iptc4xmpExt:Role>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePromotesAdditionalStandardStructuredChildScalars)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry city;
    city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrCity");
    city.value = make_text(store.arena(), "Paris",
                           TextEncoding::Utf8);
    city.origin.block          = block;
    city.origin.order_in_block = 0;
    (void)store.add_entry(city);

    Entry cv_term_name;
    cv_term_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "AboutCvTerm[1]/CvTermName");
    cv_term_name.value = make_text(store.arena(), "Culture",
                                   TextEncoding::Utf8);
    cv_term_name.origin.block          = block;
    cv_term_name.origin.order_in_block = 1;
    (void)store.add_entry(cv_term_name);

    Entry person_heard_name;
    person_heard_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonHeard[1]/Name");
    person_heard_name.value = make_text(store.arena(), "Witness",
                                        TextEncoding::Utf8);
    person_heard_name.origin.block          = block;
    person_heard_name.origin.order_in_block = 2;
    (void)store.add_entry(person_heard_name);

    Entry link_qualifier;
    link_qualifier.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "DopesheetLink[1]/LinkQualifier");
    link_qualifier.value = make_text(store.arena(), "keyframe",
                                     TextEncoding::Utf8);
    link_qualifier.origin.block          = block;
    link_qualifier.origin.order_in_block = 3;
    (void)store.add_entry(link_qualifier);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpCore:CiAdrCity>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Paris</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:CvTermName>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Culture</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:Name>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Witness</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LinkQualifier>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>keyframe</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpCore:CiAdrCity>Paris</Iptc4xmpCore:CiAdrCity>"),
              std::string_view::npos);
    EXPECT_EQ(
        s.find("<Iptc4xmpExt:CvTermName>Culture</Iptc4xmpExt:CvTermName>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:Name>Witness</Iptc4xmpExt:Name>"),
              std::string_view::npos);
    EXPECT_EQ(
        s.find("<Iptc4xmpExt:LinkQualifier>keyframe</Iptc4xmpExt:LinkQualifier>"),
        std::string_view::npos);
}

TEST(XmpDump, PortableRepairsAdditionalStructuredChildCrossShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry city_indexed;
    city_indexed.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrCity[1]");
    city_indexed.value = make_text(store.arena(), "Paris",
                                   TextEncoding::Utf8);
    city_indexed.origin.block          = block;
    city_indexed.origin.order_in_block = 0;
    (void)store.add_entry(city_indexed);

    Entry extadr_lang;
    extadr_lang.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrExtadr[@xml:lang=x-default]");
    extadr_lang.value = make_text(store.arena(), "Line 1",
                                  TextEncoding::Utf8);
    extadr_lang.origin.block          = block;
    extadr_lang.origin.order_in_block = 1;
    (void)store.add_entry(extadr_lang);

    Entry cv_term_name_indexed;
    cv_term_name_indexed.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "AboutCvTerm[1]/CvTermName[1]");
    cv_term_name_indexed.value = make_text(store.arena(), "Culture",
                                           TextEncoding::Utf8);
    cv_term_name_indexed.origin.block          = block;
    cv_term_name_indexed.origin.order_in_block = 2;
    (void)store.add_entry(cv_term_name_indexed);

    Entry link_qualifier_lang;
    link_qualifier_lang.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "DopesheetLink[1]/LinkQualifier[@xml:lang=x-default]");
    link_qualifier_lang.value = make_text(store.arena(), "keyframe",
                                          TextEncoding::Utf8);
    link_qualifier_lang.origin.block          = block;
    link_qualifier_lang.origin.order_in_block = 3;
    (void)store.add_entry(link_qualifier_lang);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpCore:CiAdrCity>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Paris</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:CiAdrExtadr>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Line 1</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:CvTermName>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Culture</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LinkQualifier>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>keyframe</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li>Paris</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li xml:lang=\"x-default\">Line 1</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li>Culture</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li xml:lang=\"x-default\">keyframe</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePromotesManagedFlatBaseScalarsToCanonicalShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry title;
    title.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "title");
    title.value = make_text(store.arena(), "Legacy Title",
                            TextEncoding::Utf8);
    title.origin.block          = block;
    title.origin.order_in_block = 0;
    (void)store.add_entry(title);

    Entry subject;
    subject.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "subject");
    subject.value = make_text(store.arena(), "legacy-subject",
                              TextEncoding::Utf8);
    subject.origin.block          = block;
    subject.origin.order_in_block = 1;
    (void)store.add_entry(subject);

    Entry description;
    description.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "description");
    description.value = make_text(store.arena(), "Legacy Description",
                                  TextEncoding::Utf8);
    description.origin.block          = block;
    description.origin.order_in_block = 2;
    (void)store.add_entry(description);

    Entry rights;
    rights.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "rights");
    rights.value = make_text(store.arena(), "Legacy Rights",
                             TextEncoding::Utf8);
    rights.origin.block          = block;
    rights.origin.order_in_block = 3;
    (void)store.add_entry(rights);

    Entry creator;
    creator.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "creator");
    creator.value = make_text(store.arena(), "Legacy Creator",
                              TextEncoding::Utf8);
    creator.origin.block          = block;
    creator.origin.order_in_block = 4;
    (void)store.add_entry(creator);

    Entry supplemental_categories;
    supplemental_categories.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/photoshop/1.0/",
        "SupplementalCategories");
    supplemental_categories.value = make_text(store.arena(), "legacy-supp",
                                              TextEncoding::Utf8);
    supplemental_categories.origin.block          = block;
    supplemental_categories.origin.order_in_block = 5;
    (void)store.add_entry(supplemental_categories);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:title>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Legacy Title</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<dc:subject>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>legacy-subject</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Legacy Description</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Legacy Rights</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<dc:creator>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Legacy Creator</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<photoshop:SupplementalCategories>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>legacy-supp</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:title>Legacy Title</dc:title>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:subject>legacy-subject</dc:subject>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:description>Legacy Description</dc:description>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:rights>Legacy Rights</dc:rights>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:creator>Legacy Creator</dc:creator>"),
              std::string_view::npos);
    EXPECT_EQ(
        s.find("<photoshop:SupplementalCategories>legacy-supp</photoshop:SupplementalCategories>"),
        std::string_view::npos);
}

TEST(XmpDump, PortablePromotesXmpRightsUsageTermsScalarToLangAlt)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry usage_terms;
    usage_terms.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/",
        "UsageTerms");
    usage_terms.value = make_text(store.arena(), "Licensed use only",
                                  TextEncoding::Utf8);
    usage_terms.origin.block          = block;
    usage_terms.origin.order_in_block = 0;
    (void)store.add_entry(usage_terms);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(2048);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<xmpRights:UsageTerms>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Alt>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Licensed use only</rdf:li>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find("<xmpRights:UsageTerms>Licensed use only</xmpRights:UsageTerms>"),
        std::string_view::npos);
}

TEST(XmpDump, PortablePromotesStandardFlatBaseScalarsToIndexedShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry language;
    language.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "language");
    language.value = make_text(store.arena(), "en-US", TextEncoding::Utf8);
    language.origin.block          = block;
    language.origin.order_in_block = 0;
    (void)store.add_entry(language);

    Entry contributor;
    contributor.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "contributor");
    contributor.value = make_text(store.arena(), "Alice", TextEncoding::Utf8);
    contributor.origin.block          = block;
    contributor.origin.order_in_block = 1;
    (void)store.add_entry(contributor);

    Entry publisher;
    publisher.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "publisher");
    publisher.value = make_text(store.arena(), "OpenMeta Press",
                                TextEncoding::Utf8);
    publisher.origin.block          = block;
    publisher.origin.order_in_block = 2;
    (void)store.add_entry(publisher);

    Entry relation;
    relation.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "relation");
    relation.value = make_text(store.arena(), "urn:related:test",
                               TextEncoding::Utf8);
    relation.origin.block          = block;
    relation.origin.order_in_block = 3;
    (void)store.add_entry(relation);

    Entry type;
    type.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "type");
    type.value = make_text(store.arena(), "Image", TextEncoding::Utf8);
    type.origin.block          = block;
    type.origin.order_in_block = 4;
    (void)store.add_entry(type);

    Entry date;
    date.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "date");
    date.value = make_text(store.arena(), "2026-04-15", TextEncoding::Utf8);
    date.origin.block          = block;
    date.origin.order_in_block = 5;
    (void)store.add_entry(date);

    Entry identifier;
    identifier.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "Identifier");
    identifier.value = make_text(store.arena(), "urn:om:test:id",
                                 TextEncoding::Utf8);
    identifier.origin.block          = block;
    identifier.origin.order_in_block = 6;
    (void)store.add_entry(identifier);

    Entry advisory;
    advisory.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "Advisory");
    advisory.value = make_text(store.arena(), "photoshop:City",
                               TextEncoding::Utf8);
    advisory.origin.block          = block;
    advisory.origin.order_in_block = 7;
    (void)store.add_entry(advisory);

    Entry owner;
    owner.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/", "Owner");
    owner.value = make_text(store.arena(), "OpenMeta Labs",
                            TextEncoding::Utf8);
    owner.origin.block          = block;
    owner.origin.order_in_block = 8;
    (void)store.add_entry(owner);

    Entry hierarchical_subject;
    hierarchical_subject.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/lightroom/1.0/",
        "hierarchicalSubject");
    hierarchical_subject.value = make_text(store.arena(), "Places|Museum",
                                           TextEncoding::Utf8);
    hierarchical_subject.origin.block          = block;
    hierarchical_subject.origin.order_in_block = 9;
    (void)store.add_entry(hierarchical_subject);

    Entry alteration_constraints;
    alteration_constraints.key = make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "ImageAlterationConstraints");
    alteration_constraints.value = make_text(store.arena(), "No compositing",
                                             TextEncoding::Utf8);
    alteration_constraints.origin.block          = block;
    alteration_constraints.origin.order_in_block = 10;
    (void)store.add_entry(alteration_constraints);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(12288);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:language>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>en-US</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<dc:contributor>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Alice</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<dc:publisher>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>OpenMeta Press</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<dc:relation>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>urn:related:test</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<dc:type>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Image</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<dc:date>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>2026-04-15</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<xmp:Identifier>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>urn:om:test:id</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmp:Advisory>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>photoshop:City</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<xmpRights:Owner>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>OpenMeta Labs</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<lr:hierarchicalSubject>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Places|Museum</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<plus:ImageAlterationConstraints>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>No compositing</rdf:li>"),
              std::string_view::npos);

    EXPECT_EQ(s.find("<dc:language>en-US</dc:language>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:contributor>Alice</dc:contributor>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:publisher>OpenMeta Press</dc:publisher>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:relation>urn:related:test</dc:relation>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<dc:type>Image</dc:type>"), std::string_view::npos);
    EXPECT_EQ(s.find("<dc:date>2026-04-15</dc:date>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmp:Identifier>urn:om:test:id</xmp:Identifier>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmp:Advisory>photoshop:City</xmp:Advisory>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpRights:Owner>OpenMeta Labs</xmpRights:Owner>"),
              std::string_view::npos);
    EXPECT_EQ(
        s.find("<lr:hierarchicalSubject>Places|Museum</lr:hierarchicalSubject>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find("<plus:ImageAlterationConstraints>No compositing</plus:ImageAlterationConstraints>"),
        std::string_view::npos);
}

TEST(XmpDump, PortableRepairsTopLevelStandardCrossShapeProperties)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry title;
    title.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "title[1]");
    title.value = make_text(store.arena(), "Legacy Title",
                            TextEncoding::Utf8);
    title.origin.block          = block;
    title.origin.order_in_block = 0;
    (void)store.add_entry(title);

    Entry identifier;
    identifier.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/",
        "Identifier[@xml:lang=x-default]");
    identifier.value = make_text(store.arena(), "urn:om:test:id",
                                 TextEncoding::Utf8);
    identifier.origin.block          = block;
    identifier.origin.order_in_block = 1;
    (void)store.add_entry(identifier);

    Entry usage_terms;
    usage_terms.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/",
        "UsageTerms[1]");
    usage_terms.value = make_text(store.arena(), "Licensed use only",
                                  TextEncoding::Utf8);
    usage_terms.origin.block          = block;
    usage_terms.origin.order_in_block = 2;
    (void)store.add_entry(usage_terms);

    Entry owner;
    owner.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/",
        "Owner[@xml:lang=x-default]");
    owner.value = make_text(store.arena(), "OpenMeta Labs",
                            TextEncoding::Utf8);
    owner.origin.block          = block;
    owner.origin.order_in_block = 3;
    (void)store.add_entry(owner);

    Entry supplemental_categories;
    supplemental_categories.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/photoshop/1.0/",
        "SupplementalCategories[@xml:lang=x-default]");
    supplemental_categories.value = make_text(
        store.arena(), "legacy-supp", TextEncoding::Utf8);
    supplemental_categories.origin.block          = block;
    supplemental_categories.origin.order_in_block = 4;
    (void)store.add_entry(supplemental_categories);

    Entry hierarchical_subject;
    hierarchical_subject.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/lightroom/1.0/",
        "hierarchicalSubject[@xml:lang=x-default]");
    hierarchical_subject.value = make_text(store.arena(), "Places|Museum",
                                           TextEncoding::Utf8);
    hierarchical_subject.origin.block          = block;
    hierarchical_subject.origin.order_in_block = 5;
    (void)store.add_entry(hierarchical_subject);

    Entry alteration_constraints;
    alteration_constraints.key = make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "ImageAlterationConstraints[@xml:lang=x-default]");
    alteration_constraints.value = make_text(store.arena(), "No compositing",
                                             TextEncoding::Utf8);
    alteration_constraints.origin.block          = block;
    alteration_constraints.origin.order_in_block = 6;
    (void)store.add_entry(alteration_constraints);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(12288);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:title>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Alt>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Legacy Title</rdf:li>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("<dc:title><rdf:Seq>"), std::string_view::npos);

    EXPECT_NE(s.find("<xmp:Identifier>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>urn:om:test:id</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmp:Identifier><rdf:Alt>"), std::string_view::npos);

    EXPECT_NE(s.find("<xmpRights:UsageTerms>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Licensed use only</rdf:li>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("<xmpRights:UsageTerms><rdf:Seq>"),
              std::string_view::npos);

    EXPECT_NE(s.find("<xmpRights:Owner>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>OpenMeta Labs</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<xmpRights:Owner><rdf:Alt>"), std::string_view::npos);

    EXPECT_NE(s.find("<photoshop:SupplementalCategories>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>legacy-supp</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(
        s.find("<photoshop:SupplementalCategories><rdf:Alt>"),
        std::string_view::npos);

    EXPECT_NE(s.find("<lr:hierarchicalSubject>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Places|Museum</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<lr:hierarchicalSubject><rdf:Alt>"),
              std::string_view::npos);

    EXPECT_NE(s.find("<plus:ImageAlterationConstraints>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>No compositing</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(
        s.find("<plus:ImageAlterationConstraints><rdf:Alt>"),
        std::string_view::npos);
}

TEST(XmpDump, PortablePromotesRemainingIptcExtStructuredChildScalars)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry contributor_name;
    contributor_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Contributor[1]/Name");
    contributor_name.value = make_text(store.arena(), "Desk Editor",
                                       TextEncoding::Utf8);
    contributor_name.origin.block          = block;
    contributor_name.origin.order_in_block = 0;
    (void)store.add_entry(contributor_name);

    Entry contributor_role;
    contributor_role.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Contributor[1]/Role");
    contributor_role.value = make_text(store.arena(), "editor",
                                       TextEncoding::Utf8);
    contributor_role.origin.block          = block;
    contributor_role.origin.order_in_block = 1;
    (void)store.add_entry(contributor_role);

    Entry planning_name;
    planning_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PlanningRef[1]/Name");
    planning_name.value = make_text(store.arena(), "Editorial Plan",
                                    TextEncoding::Utf8);
    planning_name.origin.block          = block;
    planning_name.origin.order_in_block = 2;
    (void)store.add_entry(planning_name);

    Entry planning_role;
    planning_role.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PlanningRef[1]/Role");
    planning_role.value = make_text(store.arena(), "assignment",
                                    TextEncoding::Utf8);
    planning_role.origin.block          = block;
    planning_role.origin.order_in_block = 3;
    (void)store.add_entry(planning_role);

    Entry shown_event_name;
    shown_event_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ShownEvent[1]/Name");
    shown_event_name.value = make_text(store.arena(), "Press Conference",
                                       TextEncoding::Utf8);
    shown_event_name.origin.block          = block;
    shown_event_name.origin.order_in_block = 4;
    (void)store.add_entry(shown_event_name);

    Entry supply_chain_source_name;
    supply_chain_source_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "SupplyChainSource[1]/Name");
    supply_chain_source_name.value = make_text(store.arena(), "Agency Feed",
                                               TextEncoding::Utf8);
    supply_chain_source_name.origin.block          = block;
    supply_chain_source_name.origin.order_in_block = 5;
    (void)store.add_entry(supply_chain_source_name);

    Entry video_shot_type_name;
    video_shot_type_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "VideoShotType[1]/Name");
    video_shot_type_name.value = make_text(store.arena(), "Interview",
                                           TextEncoding::Utf8);
    video_shot_type_name.origin.block          = block;
    video_shot_type_name.origin.order_in_block = 6;
    (void)store.add_entry(video_shot_type_name);

    Entry snapshot_qualifier;
    snapshot_qualifier.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Snapshot[1]/LinkQualifier");
    snapshot_qualifier.value = make_text(store.arena(), "frame-001",
                                         TextEncoding::Utf8);
    snapshot_qualifier.origin.block          = block;
    snapshot_qualifier.origin.order_in_block = 7;
    (void)store.add_entry(snapshot_qualifier);

    Entry transcript_qualifier;
    transcript_qualifier.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "TranscriptLink[1]/LinkQualifier");
    transcript_qualifier.value = make_text(store.arena(), "quote",
                                           TextEncoding::Utf8);
    transcript_qualifier.origin.block          = block;
    transcript_qualifier.origin.order_in_block = 8;
    (void)store.add_entry(transcript_qualifier);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(12288);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:Contributor>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Desk Editor</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>editor</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:PlanningRef>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Editorial Plan</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>assignment</rdf:li>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Press Conference</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Agency Feed</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Interview</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>frame-001</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>quote</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:Name>Desk Editor</Iptc4xmpExt:Name>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:Role>editor</Iptc4xmpExt:Role>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:Name>Editorial Plan</Iptc4xmpExt:Name>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:Role>assignment</Iptc4xmpExt:Role>"),
              std::string_view::npos);
    EXPECT_EQ(
        s.find("<Iptc4xmpExt:Name>Press Conference</Iptc4xmpExt:Name>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:Name>Agency Feed</Iptc4xmpExt:Name>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpExt:Name>Interview</Iptc4xmpExt:Name>"),
              std::string_view::npos);
    EXPECT_EQ(
        s.find("<Iptc4xmpExt:LinkQualifier>frame-001</Iptc4xmpExt:LinkQualifier>"),
        std::string_view::npos);
    EXPECT_EQ(
        s.find("<Iptc4xmpExt:LinkQualifier>quote</Iptc4xmpExt:LinkQualifier>"),
        std::string_view::npos);
}

TEST(XmpDump,
     PortableCanonicalizeManagedDropsMalformedLangAltWhenGeneratedScalarExists)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string location = "Louvre";
    Entry iptc_location;
    iptc_location.key = make_iptc_dataset_key(2U, 92U);
    iptc_location.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       location.data()),
                                   location.size()));
    iptc_location.origin.block          = block;
    iptc_location.origin.order_in_block = 0;
    (void)store.add_entry(iptc_location);

    Entry legacy_location;
    legacy_location.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "Location[@xml:lang=x-default]");
    legacy_location.value = make_text(store.arena(), "Legacy location",
                                      TextEncoding::Utf8);
    legacy_location.origin.block          = block;
    legacy_location.origin.order_in_block = 1;
    (void)store.add_entry(legacy_location);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpCore:Location>Louvre</Iptc4xmpCore:Location>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("Legacy location"), std::string_view::npos);
}

TEST(XmpDump,
     PortableCanonicalizeManagedDropsMalformedCrossShapeManagedPropertiesWhenGeneratedExists)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string title = "Generated Title";
    Entry iptc_title;
    iptc_title.key = make_iptc_dataset_key(2U, 5U);
    iptc_title.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(title.data()), title.size()));
    iptc_title.origin.block          = block;
    iptc_title.origin.order_in_block = 0;
    (void)store.add_entry(iptc_title);

    const std::string description = "Generated Description";
    Entry iptc_description;
    iptc_description.key = make_iptc_dataset_key(2U, 120U);
    iptc_description.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       description.data()),
                                   description.size()));
    iptc_description.origin.block          = block;
    iptc_description.origin.order_in_block = 1;
    (void)store.add_entry(iptc_description);

    const std::string rights = "Generated Rights";
    Entry iptc_rights;
    iptc_rights.key = make_iptc_dataset_key(2U, 116U);
    iptc_rights.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(rights.data()), rights.size()));
    iptc_rights.origin.block          = block;
    iptc_rights.origin.order_in_block = 2;
    (void)store.add_entry(iptc_rights);

    const std::string creator = "Generated Creator";
    Entry iptc_creator;
    iptc_creator.key = make_iptc_dataset_key(2U, 80U);
    iptc_creator.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(creator.data()),
            creator.size()));
    iptc_creator.origin.block          = block;
    iptc_creator.origin.order_in_block = 3;
    (void)store.add_entry(iptc_creator);

    const std::string keyword = "generated-keyword";
    Entry iptc_keyword;
    iptc_keyword.key = make_iptc_dataset_key(2U, 25U);
    iptc_keyword.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(keyword.data()),
            keyword.size()));
    iptc_keyword.origin.block          = block;
    iptc_keyword.origin.order_in_block = 4;
    (void)store.add_entry(iptc_keyword);

    const std::string supplemental_category = "generated-supp";
    Entry iptc_supplemental_category;
    iptc_supplemental_category.key = make_iptc_dataset_key(2U, 20U);
    iptc_supplemental_category.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       supplemental_category.data()),
                                   supplemental_category.size()));
    iptc_supplemental_category.origin.block          = block;
    iptc_supplemental_category.origin.order_in_block = 5;
    (void)store.add_entry(iptc_supplemental_category);

    Entry legacy_title;
    legacy_title.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "title[1]");
    legacy_title.value = make_text(store.arena(), "Legacy Title",
                                   TextEncoding::Utf8);
    legacy_title.origin.block          = block;
    legacy_title.origin.order_in_block = 6;
    (void)store.add_entry(legacy_title);

    Entry legacy_description;
    legacy_description.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "description[1]");
    legacy_description.value = make_text(store.arena(), "Legacy Description",
                                         TextEncoding::Utf8);
    legacy_description.origin.block          = block;
    legacy_description.origin.order_in_block = 7;
    (void)store.add_entry(legacy_description);

    Entry legacy_rights;
    legacy_rights.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "rights[1]");
    legacy_rights.value = make_text(store.arena(), "Legacy Rights",
                                    TextEncoding::Utf8);
    legacy_rights.origin.block          = block;
    legacy_rights.origin.order_in_block = 8;
    (void)store.add_entry(legacy_rights);

    Entry legacy_creator;
    legacy_creator.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "creator[@xml:lang=x-default]");
    legacy_creator.value = make_text(store.arena(), "Legacy Creator",
                                     TextEncoding::Utf8);
    legacy_creator.origin.block          = block;
    legacy_creator.origin.order_in_block = 9;
    (void)store.add_entry(legacy_creator);

    Entry legacy_subject;
    legacy_subject.key = make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "subject[@xml:lang=x-default]");
    legacy_subject.value = make_text(store.arena(), "Legacy Subject",
                                     TextEncoding::Utf8);
    legacy_subject.origin.block          = block;
    legacy_subject.origin.order_in_block = 10;
    (void)store.add_entry(legacy_subject);

    Entry legacy_supplemental_categories;
    legacy_supplemental_categories.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/photoshop/1.0/",
        "SupplementalCategories[@xml:lang=x-default]");
    legacy_supplemental_categories.value = make_text(
        store.arena(), "Legacy Supplemental Categories",
        TextEncoding::Utf8);
    legacy_supplemental_categories.origin.block          = block;
    legacy_supplemental_categories.origin.order_in_block = 11;
    (void)store.add_entry(legacy_supplemental_categories);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(16384);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<dc:title>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Generated Title</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<dc:description>"), std::string_view::npos);
    EXPECT_NE(
        s.find(
            "<rdf:li xml:lang=\"x-default\">Generated Description</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<dc:rights>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li xml:lang=\"x-default\">Generated Rights</rdf:li>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<dc:creator>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Generated Creator</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<dc:subject>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>generated-keyword</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<photoshop:SupplementalCategories>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>generated-supp</rdf:li>"),
              std::string_view::npos);

    EXPECT_EQ(s.find("Legacy Title"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Description"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Rights"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Creator"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Subject"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Supplemental Categories"),
              std::string_view::npos);
}

TEST(XmpDump,
     PortableCanonicalizeManagedReconcilesAdditionalIptcGeneratedScalarNamespaces)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string city = "Paris";
    Entry iptc_city;
    iptc_city.key = make_iptc_dataset_key(2U, 90U);
    iptc_city.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(city.data()), city.size()));
    iptc_city.origin.block          = block;
    iptc_city.origin.order_in_block = 0;
    (void)store.add_entry(iptc_city);

    const std::string state = "Ile-de-France";
    Entry iptc_state;
    iptc_state.key = make_iptc_dataset_key(2U, 95U);
    iptc_state.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(state.data()), state.size()));
    iptc_state.origin.block          = block;
    iptc_state.origin.order_in_block = 1;
    (void)store.add_entry(iptc_state);

    const std::string country_code = "FR";
    Entry iptc_country_code;
    iptc_country_code.key = make_iptc_dataset_key(2U, 100U);
    iptc_country_code.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       country_code.data()),
                                   country_code.size()));
    iptc_country_code.origin.block          = block;
    iptc_country_code.origin.order_in_block = 2;
    (void)store.add_entry(iptc_country_code);

    const std::string country = "France";
    Entry iptc_country;
    iptc_country.key = make_iptc_dataset_key(2U, 101U);
    iptc_country.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       country.data()),
                                   country.size()));
    iptc_country.origin.block          = block;
    iptc_country.origin.order_in_block = 3;
    (void)store.add_entry(iptc_country);

    const std::string headline = "Generated Headline";
    Entry iptc_headline;
    iptc_headline.key = make_iptc_dataset_key(2U, 105U);
    iptc_headline.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       headline.data()),
                                   headline.size()));
    iptc_headline.origin.block          = block;
    iptc_headline.origin.order_in_block = 4;
    (void)store.add_entry(iptc_headline);

    const std::string credit = "Generated Credit";
    Entry iptc_credit;
    iptc_credit.key = make_iptc_dataset_key(2U, 110U);
    iptc_credit.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       credit.data()),
                                   credit.size()));
    iptc_credit.origin.block          = block;
    iptc_credit.origin.order_in_block = 5;
    (void)store.add_entry(iptc_credit);

    Entry legacy_city;
    legacy_city.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/photoshop/1.0/",
        "City[@xml:lang=x-default]");
    legacy_city.value = make_text(store.arena(), "Legacy City",
                                  TextEncoding::Utf8);
    legacy_city.origin.block          = block;
    legacy_city.origin.order_in_block = 6;
    (void)store.add_entry(legacy_city);

    Entry legacy_state;
    legacy_state.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/photoshop/1.0/",
        "State[1]");
    legacy_state.value = make_text(store.arena(), "Legacy State",
                                   TextEncoding::Utf8);
    legacy_state.origin.block          = block;
    legacy_state.origin.order_in_block = 7;
    (void)store.add_entry(legacy_state);

    Entry legacy_country_code;
    legacy_country_code.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CountryCode/LegacyStructured");
    legacy_country_code.value = make_text(store.arena(), "Legacy Code",
                                          TextEncoding::Utf8);
    legacy_country_code.origin.block          = block;
    legacy_country_code.origin.order_in_block = 8;
    (void)store.add_entry(legacy_country_code);

    Entry legacy_country;
    legacy_country.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/photoshop/1.0/",
        "Country/LegacyStructured");
    legacy_country.value = make_text(store.arena(), "Legacy Country",
                                     TextEncoding::Utf8);
    legacy_country.origin.block          = block;
    legacy_country.origin.order_in_block = 9;
    (void)store.add_entry(legacy_country);

    Entry legacy_headline;
    legacy_headline.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/photoshop/1.0/",
        "Headline[@xml:lang=x-default]");
    legacy_headline.value = make_text(store.arena(), "Legacy Headline",
                                      TextEncoding::Utf8);
    legacy_headline.origin.block          = block;
    legacy_headline.origin.order_in_block = 10;
    (void)store.add_entry(legacy_headline);

    Entry legacy_credit;
    legacy_credit.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/photoshop/1.0/",
        "Credit[1]");
    legacy_credit.value = make_text(store.arena(), "Legacy Credit",
                                    TextEncoding::Utf8);
    legacy_credit.origin.block          = block;
    legacy_credit.origin.order_in_block = 11;
    (void)store.add_entry(legacy_credit);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(16384);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<photoshop:City>Paris</photoshop:City>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<photoshop:State>Ile-de-France</photoshop:State>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CountryCode>FR</Iptc4xmpCore:CountryCode>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<photoshop:Country>France</photoshop:Country>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<photoshop:Headline>Generated Headline</photoshop:Headline>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<photoshop:Credit>Generated Credit</photoshop:Credit>"),
              std::string_view::npos);

    EXPECT_EQ(s.find("Legacy City"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy State"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Code"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Country"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Headline"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Credit"), std::string_view::npos);
    EXPECT_EQ(s.find("<photoshop:LegacyStructured>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpCore:LegacyStructured>"),
              std::string_view::npos);
}

TEST(XmpDump,
     PortableCanonicalizeManagedReplacesGeneratedStructuredLocationCreated)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string city = "Paris";
    Entry iptc_city;
    iptc_city.key = make_iptc_dataset_key(2U, 90U);
    iptc_city.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(city.data()), city.size()));
    iptc_city.origin.block          = block;
    iptc_city.origin.order_in_block = 0;
    (void)store.add_entry(iptc_city);

    const std::string sub_location = "Louvre Wing";
    Entry iptc_sub_location;
    iptc_sub_location.key = make_iptc_dataset_key(2U, 92U);
    iptc_sub_location.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(sub_location.data()),
            sub_location.size()));
    iptc_sub_location.origin.block          = block;
    iptc_sub_location.origin.order_in_block = 1;
    (void)store.add_entry(iptc_sub_location);

    const std::string country_code = "FR";
    Entry iptc_country_code;
    iptc_country_code.key = make_iptc_dataset_key(2U, 100U);
    iptc_country_code.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(country_code.data()),
            country_code.size()));
    iptc_country_code.origin.block          = block;
    iptc_country_code.origin.order_in_block = 2;
    (void)store.add_entry(iptc_country_code);

    const std::string country_name = "France";
    Entry iptc_country_name;
    iptc_country_name.key = make_iptc_dataset_key(2U, 101U);
    iptc_country_name.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(country_name.data()),
            country_name.size()));
    iptc_country_name.origin.block          = block;
    iptc_country_name.origin.order_in_block = 3;
    (void)store.add_entry(iptc_country_name);

    Entry legacy_city;
    legacy_city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "LocationCreated/City");
    legacy_city.value = make_text(store.arena(), "Legacy City",
                                  TextEncoding::Utf8);
    legacy_city.origin.block          = block;
    legacy_city.origin.order_in_block = 4;
    (void)store.add_entry(legacy_city);

    Entry legacy_location;
    legacy_location.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "LocationCreated/Location");
    legacy_location.value = make_text(store.arena(), "Legacy Wing",
                                      TextEncoding::Utf8);
    legacy_location.origin.block          = block;
    legacy_location.origin.order_in_block = 5;
    (void)store.add_entry(legacy_location);

    Entry legacy_country_code;
    legacy_country_code.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "LocationCreated/CountryCode");
    legacy_country_code.value = make_text(store.arena(), "XX",
                                          TextEncoding::Utf8);
    legacy_country_code.origin.block          = block;
    legacy_country_code.origin.order_in_block = 6;
    (void)store.add_entry(legacy_country_code);

    Entry legacy_country_name;
    legacy_country_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "LocationCreated/CountryName");
    legacy_country_name.value = make_text(store.arena(), "Legacy Country",
                                          TextEncoding::Utf8);
    legacy_country_name.origin.block          = block;
    legacy_country_name.origin.order_in_block = 7;
    (void)store.add_entry(legacy_country_name);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(16384);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:City>Paris</Iptc4xmpCore:City>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:Location>Louvre Wing</Iptc4xmpCore:Location>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CountryCode>FR</Iptc4xmpCore:CountryCode>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CountryName>France</Iptc4xmpCore:CountryName>"),
        std::string_view::npos);

    EXPECT_EQ(s.find("Legacy City"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Wing"), std::string_view::npos);
    EXPECT_EQ(s.find(">XX</Iptc4xmpCore:CountryCode>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Country"), std::string_view::npos);
}

TEST(XmpDump,
     PortableCanonicalizeManagedReplacesGeneratedStructuredLocationCreatedAddressAliases)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string city = "Paris";
    Entry iptc_city;
    iptc_city.key = make_iptc_dataset_key(2U, 90U);
    iptc_city.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(city.data()), city.size()));
    iptc_city.origin.block          = block;
    iptc_city.origin.order_in_block = 0U;
    (void)store.add_entry(iptc_city);

    const std::string state = "Ile-de-France";
    Entry iptc_state;
    iptc_state.key = make_iptc_dataset_key(2U, 95U);
    iptc_state.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(state.data()), state.size()));
    iptc_state.origin.block          = block;
    iptc_state.origin.order_in_block = 1U;
    (void)store.add_entry(iptc_state);

    const std::string country_code = "FR";
    Entry iptc_country_code;
    iptc_country_code.key = make_iptc_dataset_key(2U, 100U);
    iptc_country_code.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(country_code.data()),
            country_code.size()));
    iptc_country_code.origin.block          = block;
    iptc_country_code.origin.order_in_block = 2U;
    (void)store.add_entry(iptc_country_code);

    const std::string country_name = "France";
    Entry iptc_country_name;
    iptc_country_name.key = make_iptc_dataset_key(2U, 101U);
    iptc_country_name.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(country_name.data()),
            country_name.size()));
    iptc_country_name.origin.block          = block;
    iptc_country_name.origin.order_in_block = 3U;
    (void)store.add_entry(iptc_country_name);

    Entry legacy_city;
    legacy_city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/City");
    legacy_city.value = make_text(store.arena(), "Legacy City",
                                  TextEncoding::Utf8);
    legacy_city.origin.block          = block;
    legacy_city.origin.order_in_block = 4U;
    (void)store.add_entry(legacy_city);

    Entry legacy_country_name;
    legacy_country_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/CountryName");
    legacy_country_name.value = make_text(store.arena(), "Legacy Country",
                                          TextEncoding::Utf8);
    legacy_country_name.origin.block          = block;
    legacy_country_name.origin.order_in_block = 5U;
    (void)store.add_entry(legacy_country_name);

    Entry legacy_country_code;
    legacy_country_code.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/CountryCode");
    legacy_country_code.value = make_text(store.arena(), "XX",
                                          TextEncoding::Utf8);
    legacy_country_code.origin.block          = block;
    legacy_country_code.origin.order_in_block = 6U;
    (void)store.add_entry(legacy_country_code);

    Entry legacy_state;
    legacy_state.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/ProvinceState");
    legacy_state.value = make_text(store.arena(), "Legacy State",
                                   TextEncoding::Utf8);
    legacy_state.origin.block          = block;
    legacy_state.origin.order_in_block = 7U;
    (void)store.add_entry(legacy_state);

    Entry legacy_world_region;
    legacy_world_region.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/WorldRegion");
    legacy_world_region.value = make_text(store.arena(), "EMEA",
                                          TextEncoding::Utf8);
    legacy_world_region.origin.block          = block;
    legacy_world_region.origin.order_in_block = 8U;
    (void)store.add_entry(legacy_world_region);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(16384);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:City>Paris</Iptc4xmpCore:City>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:ProvinceState>Ile-de-France</Iptc4xmpCore:ProvinceState>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CountryCode>FR</Iptc4xmpCore:CountryCode>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CountryName>France</Iptc4xmpCore:CountryName>"),
        std::string_view::npos);

    EXPECT_EQ(s.find("Legacy City"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Country"), std::string_view::npos);
    EXPECT_EQ(s.find(">XX</Iptc4xmpExt:CountryCode>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("Legacy State"), std::string_view::npos);

    EXPECT_NE(
        s.find("<Iptc4xmpExt:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:WorldRegion>EMEA</Iptc4xmpExt:WorldRegion>"),
        std::string_view::npos);
}

TEST(XmpDump,
     PortableCanonicalizeManagedReplacesGeneratedStructuredLocationCreatedDirectExtAliases)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string city = "Paris";
    Entry iptc_city;
    iptc_city.key = make_iptc_dataset_key(2U, 90U);
    iptc_city.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(city.data()), city.size()));
    iptc_city.origin.block          = block;
    iptc_city.origin.order_in_block = 0U;
    (void)store.add_entry(iptc_city);

    const std::string state = "Ile-de-France";
    Entry iptc_state;
    iptc_state.key = make_iptc_dataset_key(2U, 95U);
    iptc_state.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(state.data()), state.size()));
    iptc_state.origin.block          = block;
    iptc_state.origin.order_in_block = 1U;
    (void)store.add_entry(iptc_state);

    const std::string country_code = "FR";
    Entry iptc_country_code;
    iptc_country_code.key = make_iptc_dataset_key(2U, 100U);
    iptc_country_code.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(country_code.data()),
            country_code.size()));
    iptc_country_code.origin.block          = block;
    iptc_country_code.origin.order_in_block = 2U;
    (void)store.add_entry(iptc_country_code);

    const std::string country_name = "France";
    Entry iptc_country_name;
    iptc_country_name.key = make_iptc_dataset_key(2U, 101U);
    iptc_country_name.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(country_name.data()),
            country_name.size()));
    iptc_country_name.origin.block          = block;
    iptc_country_name.origin.order_in_block = 3U;
    (void)store.add_entry(iptc_country_name);

    Entry legacy_city;
    legacy_city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/City");
    legacy_city.value = make_text(store.arena(), "Legacy City",
                                  TextEncoding::Utf8);
    legacy_city.origin.block          = block;
    legacy_city.origin.order_in_block = 4U;
    (void)store.add_entry(legacy_city);

    Entry legacy_country_name;
    legacy_country_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/CountryName");
    legacy_country_name.value = make_text(store.arena(), "Legacy Country",
                                          TextEncoding::Utf8);
    legacy_country_name.origin.block          = block;
    legacy_country_name.origin.order_in_block = 5U;
    (void)store.add_entry(legacy_country_name);

    Entry legacy_country_code;
    legacy_country_code.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/CountryCode");
    legacy_country_code.value = make_text(store.arena(), "XX",
                                          TextEncoding::Utf8);
    legacy_country_code.origin.block          = block;
    legacy_country_code.origin.order_in_block = 6U;
    (void)store.add_entry(legacy_country_code);

    Entry legacy_state;
    legacy_state.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/ProvinceState");
    legacy_state.value = make_text(store.arena(), "Legacy State",
                                   TextEncoding::Utf8);
    legacy_state.origin.block          = block;
    legacy_state.origin.order_in_block = 7U;
    (void)store.add_entry(legacy_state);

    Entry legacy_world_region;
    legacy_world_region.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/WorldRegion");
    legacy_world_region.value = make_text(store.arena(), "EMEA",
                                          TextEncoding::Utf8);
    legacy_world_region.origin.block          = block;
    legacy_world_region.origin.order_in_block = 8U;
    (void)store.add_entry(legacy_world_region);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(16384);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:City>Paris</Iptc4xmpCore:City>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:ProvinceState>Ile-de-France</Iptc4xmpCore:ProvinceState>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CountryCode>FR</Iptc4xmpCore:CountryCode>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CountryName>France</Iptc4xmpCore:CountryName>"),
        std::string_view::npos);

    EXPECT_EQ(s.find("Legacy City"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Country"), std::string_view::npos);
    EXPECT_EQ(s.find(">XX</Iptc4xmpExt:CountryCode>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("Legacy State"), std::string_view::npos);

    EXPECT_NE(
        s.find("<Iptc4xmpExt:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:WorldRegion>EMEA</Iptc4xmpExt:WorldRegion>"),
        std::string_view::npos);
}

TEST(XmpDump,
     PortableCanonicalizeManagedReplacesGeneratedStructuredLocationCreatedMalformedAliasShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string city = "Paris";
    Entry iptc_city;
    iptc_city.key = make_iptc_dataset_key(2U, 90U);
    iptc_city.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(city.data()), city.size()));
    iptc_city.origin.block          = block;
    iptc_city.origin.order_in_block = 0U;
    (void)store.add_entry(iptc_city);

    const std::string state = "Ile-de-France";
    Entry iptc_state;
    iptc_state.key = make_iptc_dataset_key(2U, 95U);
    iptc_state.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(state.data()), state.size()));
    iptc_state.origin.block          = block;
    iptc_state.origin.order_in_block = 1U;
    (void)store.add_entry(iptc_state);

    const std::string country_code = "FR";
    Entry iptc_country_code;
    iptc_country_code.key = make_iptc_dataset_key(2U, 100U);
    iptc_country_code.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(country_code.data()),
            country_code.size()));
    iptc_country_code.origin.block          = block;
    iptc_country_code.origin.order_in_block = 2U;
    (void)store.add_entry(iptc_country_code);

    const std::string country_name = "France";
    Entry iptc_country_name;
    iptc_country_name.key = make_iptc_dataset_key(2U, 101U);
    iptc_country_name.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(country_name.data()),
            country_name.size()));
    iptc_country_name.origin.block          = block;
    iptc_country_name.origin.order_in_block = 3U;
    (void)store.add_entry(iptc_country_name);

    Entry legacy_city_lang;
    legacy_city_lang.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/City[@xml:lang=x-default]");
    legacy_city_lang.value = make_text(store.arena(), "Legacy City",
                                       TextEncoding::Utf8);
    legacy_city_lang.origin.block          = block;
    legacy_city_lang.origin.order_in_block = 4U;
    (void)store.add_entry(legacy_city_lang);

    Entry legacy_country_code_indexed;
    legacy_country_code_indexed.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/CountryCode[1]");
    legacy_country_code_indexed.value = make_text(
        store.arena(), "XX", TextEncoding::Utf8);
    legacy_country_code_indexed.origin.block          = block;
    legacy_country_code_indexed.origin.order_in_block = 5U;
    (void)store.add_entry(legacy_country_code_indexed);

    Entry legacy_country_lang;
    legacy_country_lang.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/CountryName[@xml:lang=x-default]");
    legacy_country_lang.value = make_text(store.arena(), "Legacy Country",
                                          TextEncoding::Utf8);
    legacy_country_lang.origin.block          = block;
    legacy_country_lang.origin.order_in_block = 6U;
    (void)store.add_entry(legacy_country_lang);

    Entry legacy_state_indexed;
    legacy_state_indexed.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/ProvinceState[1]");
    legacy_state_indexed.value = make_text(store.arena(), "Legacy State",
                                           TextEncoding::Utf8);
    legacy_state_indexed.origin.block          = block;
    legacy_state_indexed.origin.order_in_block = 7U;
    (void)store.add_entry(legacy_state_indexed);

    Entry legacy_world_region;
    legacy_world_region.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/WorldRegion");
    legacy_world_region.value = make_text(store.arena(), "EMEA",
                                          TextEncoding::Utf8);
    legacy_world_region.origin.block          = block;
    legacy_world_region.origin.order_in_block = 8U;
    (void)store.add_entry(legacy_world_region);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = true;
    opts.include_existing_xmp = true;
    opts.conflict_policy      = XmpConflictPolicy::ExistingWins;
    opts.existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    std::vector<std::byte> out(16384);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:City>Paris</Iptc4xmpCore:City>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:ProvinceState>Ile-de-France</Iptc4xmpCore:ProvinceState>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CountryCode>FR</Iptc4xmpCore:CountryCode>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CountryName>France</Iptc4xmpCore:CountryName>"),
        std::string_view::npos);

    EXPECT_EQ(s.find("Legacy City"), std::string_view::npos);
    EXPECT_EQ(s.find(">XX</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy Country"), std::string_view::npos);
    EXPECT_EQ(s.find("Legacy State"), std::string_view::npos);

    EXPECT_NE(
        s.find("<Iptc4xmpExt:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:WorldRegion>EMEA</Iptc4xmpExt:WorldRegion>"),
        std::string_view::npos);
}

TEST(XmpDump, PortableRepairsMalformedDirectLocationCreatedAliasShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry city_lang;
    city_lang.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/City[@xml:lang=x-default]");
    city_lang.value = make_text(store.arena(), "Paris",
                                TextEncoding::Utf8);
    city_lang.origin.block          = block;
    city_lang.origin.order_in_block = 0U;
    (void)store.add_entry(city_lang);

    Entry country_code_indexed;
    country_code_indexed.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/CountryCode[1]");
    country_code_indexed.value = make_text(store.arena(), "FR",
                                           TextEncoding::Utf8);
    country_code_indexed.origin.block          = block;
    country_code_indexed.origin.order_in_block = 1U;
    (void)store.add_entry(country_code_indexed);

    Entry world_region;
    world_region.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/WorldRegion");
    world_region.value = make_text(store.arena(), "EMEA",
                                   TextEncoding::Utf8);
    world_region.origin.block          = block;
    world_region.origin.order_in_block = 2U;
    (void)store.add_entry(world_region);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpExt:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:City>Paris</Iptc4xmpExt:City>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:CountryCode>FR</Iptc4xmpExt:CountryCode>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:WorldRegion>EMEA</Iptc4xmpExt:WorldRegion>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li xml:lang=\"x-default\">Paris</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li>FR</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortableRepairsMalformedIndexedLocationShownAliasShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry city_lang;
    city_lang.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/City[@xml:lang=x-default]");
    city_lang.value = make_text(store.arena(), "Kyoto",
                                TextEncoding::Utf8);
    city_lang.origin.block          = block;
    city_lang.origin.order_in_block = 0U;
    (void)store.add_entry(city_lang);

    Entry country_code_indexed;
    country_code_indexed.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/CountryCode[1]");
    country_code_indexed.value = make_text(store.arena(), "JP",
                                           TextEncoding::Utf8);
    country_code_indexed.origin.block          = block;
    country_code_indexed.origin.order_in_block = 1U;
    (void)store.add_entry(country_code_indexed);

    Entry world_region;
    world_region.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/WorldRegion");
    world_region.value = make_text(store.arena(), "APAC",
                                   TextEncoding::Utf8);
    world_region.origin.block          = block;
    world_region.origin.order_in_block = 2U;
    (void)store.add_entry(world_region);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:CountryCode>JP</Iptc4xmpExt:CountryCode>"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:WorldRegion>APAC</Iptc4xmpExt:WorldRegion>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li xml:lang=\"x-default\">Kyoto</rdf:li>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li>JP</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortableRepairsMalformedStructuredLocationCreatedCrossShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry name_indexed;
    name_indexed.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationName[1]");
    name_indexed.value = make_text(store.arena(), "Paris",
                                   TextEncoding::Utf8);
    name_indexed.origin.block          = block;
    name_indexed.origin.order_in_block = 0U;
    (void)store.add_entry(name_indexed);

    Entry id_lang;
    id_lang.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationId[@xml:lang=x-default]");
    id_lang.value = make_text(store.arena(), "created-001",
                              TextEncoding::Utf8);
    id_lang.origin.block          = block;
    id_lang.origin.order_in_block = 1U;
    (void)store.add_entry(id_lang);

    Entry world_region;
    world_region.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/WorldRegion");
    world_region.value = make_text(store.arena(), "EMEA",
                                   TextEncoding::Utf8);
    world_region.origin.block          = block;
    world_region.origin.order_in_block = 2U;
    (void)store.add_entry(world_region);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpExt:LocationCreated rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationName>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Paris</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationId>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>created-001</rdf:li>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:WorldRegion>EMEA</Iptc4xmpExt:WorldRegion>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li>Paris</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(
        s.find("<rdf:li xml:lang=\"x-default\">created-001</rdf:li>"),
        std::string_view::npos);
}

TEST(XmpDump, PortableRepairsMalformedIndexedLocationShownCrossShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry name_indexed;
    name_indexed.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationName[1]");
    name_indexed.value = make_text(store.arena(), "Kyoto",
                                   TextEncoding::Utf8);
    name_indexed.origin.block          = block;
    name_indexed.origin.order_in_block = 0U;
    (void)store.add_entry(name_indexed);

    Entry id_lang;
    id_lang.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationId[@xml:lang=x-default]");
    id_lang.value = make_text(store.arena(), "shown-001",
                              TextEncoding::Utf8);
    id_lang.origin.block          = block;
    id_lang.origin.order_in_block = 1U;
    (void)store.add_entry(id_lang);

    Entry world_region;
    world_region.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/WorldRegion");
    world_region.value = make_text(store.arena(), "APAC",
                                   TextEncoding::Utf8);
    world_region.origin.block          = block;
    world_region.origin.order_in_block = 2U;
    (void)store.add_entry(world_region);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<rdf:li rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationName>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Kyoto</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationId>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>shown-001</rdf:li>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:WorldRegion>APAC</Iptc4xmpExt:WorldRegion>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li>Kyoto</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(
        s.find("<rdf:li xml:lang=\"x-default\">shown-001</rdf:li>"),
        std::string_view::npos);
}

TEST(XmpDump, PortablePreservesNestedStructuredChildLangAltResources)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry default_city;
    default_city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=x-default]");
    default_city.value = make_text(store.arena(), "Tokyo",
                                   TextEncoding::Utf8);
    default_city.origin.block          = block;
    default_city.origin.order_in_block = 0;
    (void)store.add_entry(default_city);

    Entry ja_city;
    ja_city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=ja-JP]");
    ja_city.value = make_text(store.arena(), "東京", TextEncoding::Utf8);
    ja_city.origin.block          = block;
    ja_city.origin.order_in_block = 1;
    (void)store.add_entry(ja_city);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:ProvinceName>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"ja-JP\">東京</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePreservesNestedStructuredChildIndexedResources)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry code1;
    code1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceCode[1]");
    code1.value = make_text(store.arena(), "13", TextEncoding::Utf8);
    code1.origin.block          = block;
    code1.origin.order_in_block = 0;
    (void)store.add_entry(code1);

    Entry code2;
    code2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceCode[2]");
    code2.value = make_text(store.arena(), "JP-13", TextEncoding::Utf8);
    code2.origin.block          = block;
    code2.origin.order_in_block = 1;
    (void)store.add_entry(code2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:ProvinceCode>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>13</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>JP-13</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortablePreservesIndexedNestedStructuredChildLangAltResources)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry default_city;
    default_city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/City[@xml:lang=x-default]");
    default_city.value = make_text(store.arena(), "Kyoto",
                                   TextEncoding::Utf8);
    default_city.origin.block          = block;
    default_city.origin.order_in_block = 0;
    (void)store.add_entry(default_city);

    Entry ja_city;
    ja_city.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/City[@xml:lang=ja-JP]");
    ja_city.value = make_text(store.arena(), "京都", TextEncoding::Utf8);
    ja_city.origin.block          = block;
    ja_city.origin.order_in_block = 1;
    (void)store.add_entry(ja_city);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:Address rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:City>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Kyoto</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"ja-JP\">京都</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePreservesIndexedNestedStructuredChildIndexedResources)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry code1;
    code1.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/CountryCode[1]");
    code1.value = make_text(store.arena(), "JP", TextEncoding::Utf8);
    code1.origin.block          = block;
    code1.origin.order_in_block = 0;
    (void)store.add_entry(code1);

    Entry code2;
    code2.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/CountryCode[2]");
    code2.value = make_text(store.arena(), "JP-26", TextEncoding::Utf8);
    code2.origin.block          = block;
    code2.origin.order_in_block = 1;
    (void)store.add_entry(code2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpExt:LocationShown>"), std::string_view::npos);
    EXPECT_NE(
        s.find("<Iptc4xmpExt:Address rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpExt:CountryCode>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>JP</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>JP-26</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump,
     PortablePromotesFlatNestedStructuredChildScalarsToCanonicalShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry province_name;
    province_name.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceName");
    province_name.value = make_text(store.arena(), "Tokyo",
                                    TextEncoding::Utf8);
    province_name.origin.block          = block;
    province_name.origin.order_in_block = 0;
    (void)store.add_entry(province_name);

    Entry province_code;
    province_code.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceCode");
    province_code.value = make_text(store.arena(), "JP-13",
                                    TextEncoding::Utf8);
    province_code.origin.block          = block;
    province_code.origin.order_in_block = 1;
    (void)store.add_entry(province_code);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<Iptc4xmpCore:ProvinceName>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:ProvinceCode>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>JP-13</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(
        s.find("<Iptc4xmpCore:ProvinceName>Tokyo</Iptc4xmpCore:ProvinceName>"),
        std::string_view::npos);
}

TEST(XmpDump, PortableRepairsNestedStructuredChildCrossShapes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry province_name_indexed;
    province_name_indexed.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceName[1]");
    province_name_indexed.value = make_text(store.arena(), "Tokyo",
                                            TextEncoding::Utf8);
    province_name_indexed.origin.block          = block;
    province_name_indexed.origin.order_in_block = 0;
    (void)store.add_entry(province_name_indexed);

    Entry province_code_lang;
    province_code_lang.key = make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceCode[@xml:lang=x-default]");
    province_code_lang.value = make_text(store.arena(), "JP-13",
                                         TextEncoding::Utf8);
    province_code_lang.origin.block          = block;
    province_code_lang.origin.order_in_block = 1;
    (void)store.add_entry(province_code_lang);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"),
        std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:ProvinceName>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:ProvinceCode>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>JP-13</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li>Tokyo</rdf:li>"), std::string_view::npos);
    EXPECT_EQ(s.find("<rdf:li xml:lang=\"x-default\">JP-13</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePreservesCrsNamespace)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry process_version;
    process_version.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/camera-raw-settings/1.0/",
        "ProcessVersion");
    process_version.value = make_text(store.arena(), "16.0",
                                      TextEncoding::Utf8);
    process_version.origin.block          = block;
    process_version.origin.order_in_block = 0;
    (void)store.add_entry(process_version);

    Entry exposure;
    exposure.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/camera-raw-settings/1.0/",
        "Exposure2012");
    exposure.value = make_text(store.arena(), "+0.35",
                               TextEncoding::Utf8);
    exposure.origin.block          = block;
    exposure.origin.order_in_block = 1;
    (void)store.add_entry(exposure);

    Entry white_balance;
    white_balance.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/camera-raw-settings/1.0/",
        "WhiteBalance");
    white_balance.value = make_text(store.arena(), "As Shot",
                                    TextEncoding::Utf8);
    white_balance.origin.block          = block;
    white_balance.origin.order_in_block = 2;
    (void)store.add_entry(white_balance);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(
        s.find("xmlns:crs=\"http://ns.adobe.com/camera-raw-settings/1.0/\""),
        std::string_view::npos);
    EXPECT_NE(s.find("<crs:ProcessVersion>16.0</crs:ProcessVersion>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<crs:Exposure2012>+0.35</crs:Exposure2012>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<crs:WhiteBalance>As Shot</crs:WhiteBalance>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePreservesDngNamespace)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry profile_name;
    profile_name.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/dng/1.0/", "ProfileName");
    profile_name.value = make_text(store.arena(), "Source Raw Profile",
                                   TextEncoding::Utf8);
    profile_name.origin.block          = block;
    profile_name.origin.order_in_block = 0;
    (void)store.add_entry(profile_name);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("xmlns:dng=\"http://ns.adobe.com/dng/1.0/\""),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<dng:ProfileName>Source Raw Profile</dng:ProfileName>"),
        std::string_view::npos);
}

TEST(XmpDump, PortablePreservesLrNamespace)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry private_rtk_info;
    private_rtk_info.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/lightroom/1.0/",
        "PrivateRTKInfo");
    private_rtk_info.value = make_text(store.arena(), "face-region-cache",
                                       TextEncoding::Utf8);
    private_rtk_info.origin.block          = block;
    private_rtk_info.origin.order_in_block = 0;
    (void)store.add_entry(private_rtk_info);

    Entry private_rtk_flag;
    private_rtk_flag.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/lightroom/1.0/",
        "PrivateRTKFlag");
    private_rtk_flag.value = make_text(store.arena(), "true",
                                       TextEncoding::Utf8);
    private_rtk_flag.origin.block          = block;
    private_rtk_flag.origin.order_in_block = 1;
    (void)store.add_entry(private_rtk_flag);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(1024);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("xmlns:lr=\"http://ns.adobe.com/lightroom/1.0/\""),
              std::string_view::npos);
    EXPECT_NE(s.find("<lr:PrivateRTKInfo>face-region-cache</lr:PrivateRTKInfo>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<lr:PrivateRTKFlag>true</lr:PrivateRTKFlag>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableExistingLrHierarchicalSubjectEmitsBag)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry item1;
    item1.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/lightroom/1.0/",
        "hierarchicalSubject[1]");
    item1.value = make_text(store.arena(), "Places|Japan|Tokyo",
                            TextEncoding::Utf8);
    item1.origin.block          = block;
    item1.origin.order_in_block = 0;
    (void)store.add_entry(item1);

    Entry item2;
    item2.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/lightroom/1.0/",
        "hierarchicalSubject[2]");
    item2.value = make_text(store.arena(), "Travel|Spring",
                            TextEncoding::Utf8);
    item2.origin.block          = block;
    item2.origin.order_in_block = 1;
    (void)store.add_entry(item2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(1024);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<lr:hierarchicalSubject>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Places|Japan|Tokyo</rdf:li>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Travel|Spring</rdf:li>"),
              std::string_view::npos);
}

TEST(XmpDump, PortablePreservesPdfNamespace)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry keywords;
    keywords.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/pdf/1.3/", "Keywords");
    keywords.value = make_text(store.arena(), "tokyo,night,street",
                               TextEncoding::Utf8);
    keywords.origin.block          = block;
    keywords.origin.order_in_block = 0;
    (void)store.add_entry(keywords);

    Entry producer;
    producer.key = make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/pdf/1.3/", "Producer");
    producer.value = make_text(store.arena(), "OpenMetaTest",
                               TextEncoding::Utf8);
    producer.origin.block          = block;
    producer.origin.order_in_block = 1;
    (void)store.add_entry(producer);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(1024);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\""),
              std::string_view::npos);
    EXPECT_NE(s.find("<pdf:Keywords>tokyo,night,street</pdf:Keywords>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<pdf:Producer>OpenMetaTest</pdf:Producer>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableMapsIptcToPhotoshopAndIptcCoreProperties)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string category = "ART";
    Entry e0;
    e0.key          = make_iptc_dataset_key(2U, 15U);  // Category
    e0.value        = make_bytes(store.arena(), std::span<const std::byte>(
                                             reinterpret_cast<const std::byte*>(
                                                 category.data()),
                                             category.size()));
    e0.origin.block = block;
    e0.origin.order_in_block = 0;
    (void)store.add_entry(e0);

    const std::string subcat_0 = "Travel";
    Entry e1;
    e1.key          = make_iptc_dataset_key(2U, 20U);  // SupplementalCategories
    e1.value        = make_bytes(store.arena(), std::span<const std::byte>(
                                             reinterpret_cast<const std::byte*>(
                                                 subcat_0.data()),
                                             subcat_0.size()));
    e1.origin.block = block;
    e1.origin.order_in_block = 1;
    (void)store.add_entry(e1);

    const std::string subcat_1 = "Museum";
    Entry e2;
    e2.key          = make_iptc_dataset_key(2U, 20U);  // SupplementalCategories
    e2.value        = make_bytes(store.arena(), std::span<const std::byte>(
                                             reinterpret_cast<const std::byte*>(
                                                 subcat_1.data()),
                                             subcat_1.size()));
    e2.origin.block = block;
    e2.origin.order_in_block = 2;
    (void)store.add_entry(e2);

    const std::string city = "Paris";
    Entry e3;
    e3.key                   = make_iptc_dataset_key(2U, 90U);  // City
    e3.value                 = make_bytes(store.arena(),
                                          std::span<const std::byte>(
                              reinterpret_cast<const std::byte*>(city.data()),
                              city.size()));
    e3.origin.block          = block;
    e3.origin.order_in_block = 3;
    (void)store.add_entry(e3);

    const std::string location = "Louvre";
    Entry e4;
    e4.key          = make_iptc_dataset_key(2U, 92U);  // Sub-location
    e4.value        = make_bytes(store.arena(), std::span<const std::byte>(
                                             reinterpret_cast<const std::byte*>(
                                                 location.data()),
                                             location.size()));
    e4.origin.block = block;
    e4.origin.order_in_block = 4;
    (void)store.add_entry(e4);

    const std::string state = "Ile-de-France";
    Entry e5;
    e5.key          = make_iptc_dataset_key(2U, 95U);  // Province-State
    e5.value        = make_bytes(store.arena(),
                                 std::span<const std::byte>(
                              reinterpret_cast<const std::byte*>(state.data()),
                              state.size()));
    e5.origin.block = block;
    e5.origin.order_in_block = 5;
    (void)store.add_entry(e5);

    const std::string country_code = "FR";
    Entry e6;
    e6.key          = make_iptc_dataset_key(2U, 100U);  // Country code
    e6.value        = make_bytes(store.arena(), std::span<const std::byte>(
                                             reinterpret_cast<const std::byte*>(
                                                 country_code.data()),
                                             country_code.size()));
    e6.origin.block = block;
    e6.origin.order_in_block = 6;
    (void)store.add_entry(e6);

    const std::string country = "France";
    Entry e7;
    e7.key          = make_iptc_dataset_key(2U, 101U);  // Country name
    e7.value        = make_bytes(store.arena(), std::span<const std::byte>(
                                             reinterpret_cast<const std::byte*>(
                                                 country.data()),
                                             country.size()));
    e7.origin.block = block;
    e7.origin.order_in_block = 7;
    (void)store.add_entry(e7);

    const std::string headline = "Evening light";
    Entry e8;
    e8.key          = make_iptc_dataset_key(2U, 105U);  // Headline
    e8.value        = make_bytes(store.arena(), std::span<const std::byte>(
                                             reinterpret_cast<const std::byte*>(
                                                 headline.data()),
                                             headline.size()));
    e8.origin.block = block;
    e8.origin.order_in_block = 8;
    (void)store.add_entry(e8);

    const std::string credit = "OpenMeta News";
    Entry e9;
    e9.key                   = make_iptc_dataset_key(2U, 110U);  // Credit
    e9.value                 = make_bytes(store.arena(),
                                          std::span<const std::byte>(
                              reinterpret_cast<const std::byte*>(credit.data()),
                              credit.size()));
    e9.origin.block          = block;
    e9.origin.order_in_block = 9;
    (void)store.add_entry(e9);

    const std::string source = "Desk A";
    Entry e10;
    e10.key                   = make_iptc_dataset_key(2U, 115U);  // Source
    e10.value                 = make_bytes(store.arena(),
                                           std::span<const std::byte>(
                               reinterpret_cast<const std::byte*>(source.data()),
                               source.size()));
    e10.origin.block          = block;
    e10.origin.order_in_block = 10;
    (void)store.add_entry(e10);

    const std::string caption_writer = "Editor Name";
    Entry e11;
    e11.key   = make_iptc_dataset_key(2U, 122U);  // Writer-Editor
    e11.value = make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       caption_writer.data()),
                                   caption_writer.size()));
    e11.origin.block          = block;
    e11.origin.order_in_block = 11;
    (void)store.add_entry(e11);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("xmlns:photoshop=\"http://ns.adobe.com/photoshop/1.0/\""),
              std::string_view::npos);
    EXPECT_NE(
        s.find(
            "xmlns:Iptc4xmpCore=\"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/\""),
        std::string_view::npos);
    EXPECT_NE(s.find("<photoshop:Category>ART</photoshop:Category>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<photoshop:City>Paris</photoshop:City>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:Location>Louvre</Iptc4xmpCore:Location>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<photoshop:State>Ile-de-France</photoshop:State>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<Iptc4xmpCore:CountryCode>FR</Iptc4xmpCore:CountryCode>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<photoshop:Country>France</photoshop:Country>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<photoshop:Headline>Evening light</photoshop:Headline>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<photoshop:Credit>OpenMeta News</photoshop:Credit>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<photoshop:Source>Desk A</photoshop:Source>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<photoshop:CaptionWriter>Editor Name</photoshop:CaptionWriter>"),
        std::string_view::npos);

    EXPECT_NE(s.find("<photoshop:SupplementalCategories>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Bag>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Travel</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>Museum</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortableCanSuppressIptcProjection)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::string category = "ART";
    Entry e0;
    e0.key          = make_iptc_dataset_key(2U, 15U);
    e0.value        = make_bytes(store.arena(), std::span<const std::byte>(
                                             reinterpret_cast<const std::byte*>(
                                                 category.data()),
                                             category.size()));
    e0.origin.block = block;
    e0.origin.order_in_block = 0U;
    (void)store.add_entry(e0);
    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_iptc         = false;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("<photoshop:Category>ART</photoshop:Category>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<Iptc4xmpCore:"), std::string_view::npos);
}

}  // namespace openmeta
