// SPDX-License-Identifier: Apache-2.0
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/exif_tiff_serialize.h"
#include "openmeta/meta_edit.h"
#include "openmeta/metadata_translation.h"
#include "openmeta/validate.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <array>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace openmeta {
namespace {
    constexpr std::string_view kExif   = "http://ns.adobe.com/exif/1.0/";
    constexpr std::string_view kExifEx = "http://cipa.jp/exif/1.0/";
    constexpr auto kAll = MetadataCaptureTranslationSourceMode::All;
    constexpr auto kReplace
        = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
    constexpr auto kOk = MetadataCaptureTranslationStatus::Ok;

    void xmp(MetaStore& store, std::string_view name, std::string_view text,
             bool extended = false, EntryFlags flags = EntryFlags::Dirty)
    {
        Entry e;
        e.key = make_xmp_property_key(store.arena(), extended ? kExifEx : kExif,
                                      name);
        e.value = make_text(store.arena(), text, TextEncoding::Utf8);
        e.flags = flags;
        (void)store.add_entry(e);
    }
    void native(MetaStore& store, uint16_t tag, const MetaValue& value,
                std::string_view ifd = "exififd",
                EntryFlags flags     = EntryFlags::None)
    {
        Entry e;
        e.key   = make_exif_tag_key(store.arena(), ifd, tag);
        e.value = value;
        e.flags = flags;
        (void)store.add_entry(e);
    }
    MetaValue bytes(MetaStore& store, std::string_view text)
    {
        return make_bytes(store.arena(),
                          std::as_bytes(std::span(text.data(), text.size())));
    }
    const Entry* find(const MetaStore& store, uint16_t tag)
    {
        const auto ids = store.find_all(make_exif_tag_key_view("exififd", tag));
        return ids.size() == 1U ? &store.entry(ids[0]) : nullptr;
    }
    std::string raw(const MetaStore& store, uint16_t tag)
    {
        const Entry* e = find(store, tag);
        if (!e)
            return {};
        const auto data = store.arena().span(e->value.data.span);
        return { reinterpret_cast<const char*>(data.data()), data.size() };
    }
    std::string packet(const MetaStore& store, bool existing = false,
                       XmpExistingStandardNamespacePolicy policy
                       = XmpExistingStandardNamespacePolicy::PreserveAll)
    {
        std::vector<std::byte> out(262144U);
        XmpPortableOptions options;
        options.include_existing_xmp               = existing;
        options.existing_standard_namespace_policy = policy;
        const auto result = dump_xmp_portable(store, out, options);
        EXPECT_EQ(result.status, XmpDumpStatus::Ok);
        return { reinterpret_cast<const char*>(out.data()),
                 static_cast<size_t>(result.written) };
    }
    void expect_failure(MetaStore& store,
                        MetadataCaptureTranslationStatus expected,
                        MetadataExifTextTranslationOptions options = {})
    {
        store.finalize();
        const auto* entries = store.entries().data();
        const auto data     = store.arena().bytes();
        const std::vector<std::byte> before(data.begin(), data.end());
        EXPECT_EQ(
            translate_xmp_exif_text_metadata(store, options, &store).status,
            expected);
        EXPECT_EQ(store.entries().data(), entries);
        EXPECT_EQ(std::vector<std::byte>(store.arena().bytes().begin(),
                                         store.arena().bytes().end()),
                  before);
    }

    TEST(MetadataExifText, FullBatchExactUtf8WireAndRoundTrip)
    {
        MetaStore store;
        xmp(store, "ExifVersion", "0300");
        xmp(store, "FlashpixVersion", "0100");
        xmp(store, "UserComment[@xml:lang=x-default]",
            " 日本語 & <> \r\n\t 😀 ");
        constexpr std::array<std::string_view, 10> names
            = { "ImageTitle",
                "Photographer",
                "ImageEditor",
                "CameraFirmware",
                "RAWDevelopingSoftware",
                "ImageEditingSoftware",
                "MetadataEditingSoftware",
                "CameraOwnerName",
                "LensMake",
                "LensModel" };
        constexpr std::array<uint16_t, 10> tags
            = { 0xa436, 0xa437, 0xa438, 0xa439, 0xa43a,
                0xa43b, 0xa43c, 0xa430, 0xa433, 0xa434 };
        for (const auto name : names)
            xmp(store, name, " 日本語 \r\n\t ", true);
        native(store, 0x013b,
               make_text(store.arena(), "Artist", TextEncoding::Ascii), "ifd0");
        native(store, 0x0131,
               make_text(store.arena(), "Software", TextEncoding::Ascii),
               "ifd0");
        store.finalize();
        auto result = translate_xmp_exif_text_metadata(store, {}, &store);
        ASSERT_EQ(result.status, kOk);
        EXPECT_EQ(result.entries_added, 13U);
        EXPECT_TRUE(validate_store(store).ok());
        EXPECT_EQ(raw(store, 0x9000), "0300");
        EXPECT_EQ(raw(store, 0xa000), "0100");
        EXPECT_EQ(raw(store, 0x9286),
                  std::string("UNICODE\0", 8) + " 日本語 & <> \r\n\t 😀 ");
        for (uint16_t tag : tags) {
            ASSERT_NE(find(store, tag), nullptr);
            EXPECT_EQ(find(store, tag)->origin.wire_type.code, 129U);
        }
        EXPECT_EQ(translate_xmp_exif_text_metadata(store, {}, &store)
                      .groups_unchanged,
                  13U);
        const auto measured = serialize_exif_tiff(store, {});
        ASSERT_EQ(measured.status, ExifTiffSerializeStatus::OutputTruncated);
        std::vector<std::byte> tiff(measured.needed);
        ASSERT_TRUE(serialize_exif_tiff(store, tiff).ok());
        MetaStore reread;
        ASSERT_EQ(decode_exif_tiff(tiff, reread, {}, {}).status,
                  ExifDecodeStatus::Ok);
        reread.finalize();
        EXPECT_TRUE(validate_store(reread).ok());
        EXPECT_EQ(raw(reread, 0x9286), raw(store, 0x9286));
        for (uint16_t tag : tags) {
            ASSERT_NE(find(reread, tag), nullptr);
            EXPECT_EQ(find(reread, tag)->origin.wire_type.code, 129U);
            EXPECT_EQ(raw(reread, tag), raw(store, tag));
        }
        const auto xml = packet(reread);
        EXPECT_NE(xml.find("xml:lang=\"x-default\""), std::string::npos);
        EXPECT_NE(xml.find("<exifEX:ImageTitle>"), std::string::npos);
        EXPECT_NE(xml.find("&#13;&#10;&#9;"), std::string::npos);
        MetaStore restored;
        ASSERT_EQ(decode_xmp_packet(std::as_bytes(
                                        std::span(xml.data(), xml.size())),
                                    restored)
                      .status,
                  XmpDecodeStatus::Ok);
        native(restored, 0x013b,
               make_text(restored.arena(), "Artist", TextEncoding::Ascii),
               "ifd0");
        native(restored, 0x0131,
               make_text(restored.arena(), "Software", TextEncoding::Ascii),
               "ifd0");
        restored.finalize();
        ASSERT_EQ(translate_xmp_exif_text_metadata(restored,
                                                   { .source_mode = kAll },
                                                   &restored)
                      .status,
                  kOk);
        EXPECT_EQ(raw(restored, 0x9286), raw(store, 0x9286));
        for (uint16_t tag : tags)
            EXPECT_EQ(raw(restored, tag), raw(store, tag));
    }

    TEST(MetadataExifText, LegacyAsciiUtf16AndVersionTransition)
    {
        for (const auto version : { "0232", "0300" }) {
            MetaStore store;
            xmp(store, "ExifVersion", version);
            xmp(store, "UserComment", "日本 😀");
            store.finalize();
            ASSERT_EQ(translate_xmp_exif_text_metadata(store, {}, &store).status,
                      kOk);
            const auto comment = raw(store, 0x9286);
            EXPECT_EQ(comment.substr(0, 8), std::string("UNICODE\0", 8));
            if (std::string_view(version) == "0232")
                EXPECT_EQ(comment.substr(8, 2), std::string("\xff\xfe", 2));
            EXPECT_NE(packet(store).find("日本 😀"), std::string::npos);
        }
        MetaStore ascii;
        xmp(ascii, "UserComment", " a & b \r\n\t ");
        ascii.finalize();
        ASSERT_EQ(translate_xmp_exif_text_metadata(ascii, {}, &ascii).status,
                  kOk);
        EXPECT_EQ(raw(ascii, 0x9286),
                  std::string("ASCII\0\0\0", 8) + " a & b \r\n\t ");
        MetaStore upgraded;
        native(upgraded, 0x9000, bytes(upgraded, "0232"));
        native(upgraded, 0x9286,
               bytes(upgraded,
                     std::string("UNICODE\0", 8) + std::string("A\0", 2)));
        xmp(upgraded, "ExifVersion", "0300");
        expect_failure(upgraded,
                       MetadataCaptureTranslationStatus::NativeConflict,
                       { .conflict_policy = kReplace });
        MetaEdit edit;
        Entry comment;
        comment.key = make_xmp_property_key(edit.arena(), kExif, "UserComment");
        comment.value = make_text(edit.arena(), "A", TextEncoding::Ascii);
        comment.flags = EntryFlags::Dirty;
        edit.add_entry(comment);
        upgraded = commit(upgraded, std::span<const MetaEdit>(&edit, 1U));
        const auto upgraded_result = translate_xmp_exif_text_metadata(
            upgraded, { .conflict_policy = kReplace }, &upgraded);
        ASSERT_EQ(upgraded_result.status, kOk)
            << metadata_capture_translation_mapping_name(
                   upgraded_result.failed_mapping)
            << " source=" << upgraded_result.failed_source_entry
            << " properties=" << upgraded_result.source_properties;
        EXPECT_EQ(raw(upgraded, 0x9286), std::string("ASCII\0\0\0", 8) + "A");
    }

    TEST(MetadataExifText, NativeBigEndianAndExplicitBoms)
    {
        for (const bool big : { false, true }) {
            for (const bool bom : { false, true }) {
                MetaStore store;
                native(store, 0x9000, bytes(store, "0232"));
                std::string value("UNICODE\0", 8);
                if (bom)
                    value += std::string(big ? "\xfe\xff" : "\xff\xfe", 2);
                value += std::string(big ? "\x65\xe5\x67\x2c"
                                         : "\xe5\x65\x2c\x67",
                                     4);
                native(store, 0x9286, bytes(store, value), "exififd",
                       big ? EntryFlags::ValueBigEndian : EntryFlags::None);
                store.finalize();
                EXPECT_NE(packet(store).find("日本"), std::string::npos);
                const auto measured = serialize_exif_tiff(store, {});
                ASSERT_EQ(measured.status,
                          ExifTiffSerializeStatus::OutputTruncated);
                std::vector<std::byte> tiff(measured.needed);
                ASSERT_TRUE(serialize_exif_tiff(store, tiff).ok());
                MetaStore read;
                ASSERT_EQ(decode_exif_tiff(tiff, read, {}, {}).status,
                          ExifDecodeStatus::Ok);
                read.finalize();
                EXPECT_NE(packet(read).find("日本"), std::string::npos);
            }
        }
    }

    TEST(MetadataExifText,
         CanonicalizationPreservesOtherLanguagesAndRejectsMalformedNative)
    {
        for (const bool valid : { false, true }) {
            MetaStore store;
            native(store, 0x9286,
                   bytes(store, valid ? std::string("ASCII\0\0\0", 8) + "native"
                                      : "broken"));
            xmp(store, "UserComment[@xml:lang=x-default]", "existing");
            xmp(store, "UserComment[@xml:lang=ja]", "日本語");
            store.finalize();
            const auto xml = packet(
                store, true,
                XmpExistingStandardNamespacePolicy::CanonicalizeManaged);
            EXPECT_NE(xml.find("日本語"), std::string::npos);
            EXPECT_NE(xml.find(valid ? ">native<" : ">existing<"),
                      std::string::npos);
            EXPECT_EQ(xml.find("<exif:UserComment>",
                               xml.find("<exif:UserComment>") + 1U),
                      std::string::npos);
        }
    }

    TEST(MetadataExifText, CompanionVersionAndShapeFailuresRollbackTogether)
    {
        using S = MetadataCaptureTranslationStatus;
        for (const auto name :
             { "ImageTitle", "CameraOwnerName", "LensMake", "LensModel" }) {
            MetaStore store;
            xmp(store, "UserComment", "valid");
            xmp(store, name, "日本", true);
            expect_failure(store, S::IncompleteSource);
        }
        for (const auto name :
             { "Photographer", "ImageEditor", "CameraFirmware",
               "RAWDevelopingSoftware", "ImageEditingSoftware",
               "MetadataEditingSoftware" }) {
            MetaStore store;
            xmp(store, "ExifVersion", "0300");
            xmp(store, name, "explicit", true);
            expect_failure(store, S::IncompleteSource);
        }
        for (const auto value : { "030", "03x0", "03000", "3.0" }) {
            MetaStore store;
            xmp(store, "ExifVersion", value);
            expect_failure(store, S::InvalidSourceValue);
        }
        MetaStore flash;
        xmp(flash, "FlashpixVersion", "0200");
        expect_failure(flash, S::InvalidSourceValue);
        for (const auto path : { "ImageTitle[1]", "ImageTitle/Value" }) {
            MetaStore store;
            xmp(store, path, "value", true);
            expect_failure(store, S::UnsupportedSourceShape);
        }
        MetaStore duplicate;
        xmp(duplicate, "UserComment", "one");
        xmp(duplicate, "UserComment[@xml:lang=x-default]", "two");
        expect_failure(duplicate, S::AmbiguousSource);
    }

    TEST(MetadataExifText, BudgetsControlsTombstonesAndSourceModes)
    {
        using S = MetadataCaptureTranslationStatus;
        for (const auto invalid :
             { std::string("a\0b", 3), std::string("\xc0\x80", 2),
               std::string("\xed\xa0\x80", 3), std::string("\x01", 1),
               std::string("\xef\xbb\xbf", 3) }) {
            MetaStore store;
            xmp(store, "UserComment", invalid);
            expect_failure(store, S::InvalidSourceValue);
        }
        MetaStore store;
        xmp(store, "UserComment", "comment", false, EntryFlags::None);
        store.finalize();
        EXPECT_EQ(translate_xmp_exif_text_metadata(store, {}, &store)
                      .source_properties,
                  0U);
        expect_failure(store, S::ValueTooLong,
                       { .source_mode                 = kAll,
                         .max_text_bytes_per_property = 6U });
        expect_failure(store, S::SourceLimitExceeded,
                       { .source_mode = kAll, .max_total_text_bytes = 6U });
        expect_failure(store, S::EntryLimitExceeded,
                       { .source_mode = kAll, .max_added_entries = 0U });
        expect_failure(store, S::OperationLimitExceeded,
                       { .source_mode = kAll, .max_operations = 0U });
        ASSERT_EQ(translate_xmp_exif_text_metadata(store,
                                                   { .source_mode = kAll },
                                                   &store)
                      .status,
                  kOk);
        MetaStore deletion;
        native(deletion, 0x9286,
               bytes(deletion, std::string("ASCII\0\0\0", 8) + "keep"));
        xmp(deletion, "UserComment", "", false,
            EntryFlags::Dirty | EntryFlags::Deleted);
        expect_failure(deletion, S::NativeConflict);
        EXPECT_EQ(
            translate_xmp_exif_text_metadata(
                deletion,
                { .conflict_policy
                  = MetadataCaptureTranslationConflictPolicy::PreserveExisting },
                &deletion)
                .groups_preserved,
            1U);
        ASSERT_EQ(translate_xmp_exif_text_metadata(
                      deletion, { .conflict_policy = kReplace }, &deletion)
                      .status,
                  kOk);
        EXPECT_EQ(find(deletion, 0x9286), nullptr);
    }

    TEST(MetadataExifText, EmptyValuesAndNativeVersionValidation)
    {
        MetaStore store;
        xmp(store, "ExifVersion", "0300");
        xmp(store, "UserComment", "");
        xmp(store, "ImageTitle", "", true);
        store.finalize();
        ASSERT_EQ(translate_xmp_exif_text_metadata(store, {}, &store).status,
                  kOk);
        EXPECT_EQ(raw(store, 0x9286), std::string("ASCII\0\0\0", 8));
        EXPECT_NE(packet(store).find("xml:lang=\"x-default\"></rdf:li>"),
                  std::string::npos);
        EXPECT_TRUE(validate_store(store).ok());
        for (const auto tag : { 0x9000U, 0xa000U }) {
            MetaStore bad;
            native(bad, static_cast<uint16_t>(tag), bytes(bad, "03x0"));
            bad.finalize();
            EXPECT_FALSE(validate_store(bad).ok());
        }
    }

    TEST(MetadataExifText, DecoderRetainsLegacyBigEndianCommentProvenance)
    {
        // A minimal big-endian TIFF, with ExifVersion and BOM-less UTF-16.
        const std::array<unsigned char, 68> wire
            = { 'M', 'M', 0,   42,  0,    0,    0,    8,   0,   1,   0x87, 0x69,
                0,   4,   0,   0,   0,    1,    0,    0,   0,   26,  0,    0,
                0,   0,   0,   2,   0x90, 0,    0,    7,   0,   0,   0,    4,
                '0', '2', '3', '2', 0x92, 0x86, 0,    7,   0,   0,   0,    12,
                0,   0,   0,   56,  0,    0,    0,    0,   'U', 'N', 'I',  'C',
                'O', 'D', 'E', 0,   0x65, 0xe5, 0x67, 0x2c };
        MetaStore store;
        ASSERT_EQ(decode_exif_tiff(std::as_bytes(std::span(wire)), store, {}, {})
                      .status,
                  ExifDecodeStatus::Ok);
        store.finalize();
        ASSERT_NE(find(store, 0x9286), nullptr);
        EXPECT_TRUE(
            any(find(store, 0x9286)->flags, EntryFlags::ValueBigEndian));
        EXPECT_NE(packet(store).find("日本"), std::string::npos);
    }

    TEST(MetadataExifText, ExactTextLimitAndDuplicateNativeReplacement)
    {
        MetaStore source;
        const std::string text(
            kMetadataExifTextTranslationMaxTextBytesPerProperty, 'x');
        xmp(source, "UserComment", text);
        native(source, 0x9286,
               bytes(source, std::string("ASCII\0\0\0", 8) + "old"));
        native(source, 0x9286,
               bytes(source, std::string("ASCII\0\0\0", 8) + "duplicate"));
        source.finalize();
        expect_failure(source,
                       MetadataCaptureTranslationStatus::NativeConflict);
        const auto result = translate_xmp_exif_text_metadata(
            source, { .conflict_policy = kReplace }, &source);
        ASSERT_EQ(result.status, kOk);
        EXPECT_EQ(result.entries_updated, 1U);
        EXPECT_EQ(result.entries_removed, 1U);
        EXPECT_EQ(raw(source, 0x9286).size(), text.size() + 8U);
        MetaStore too_long;
        xmp(too_long, "UserComment", text + "x");
        expect_failure(too_long,
                       MetadataCaptureTranslationStatus::ValueTooLong);
    }

    TEST(MetadataExifText, MalformedNativeCommentNeverClaimsDefaultLanguage)
    {
        const std::array<std::string, 5> bad
            = { std::string("UNICODE\0", 8) + std::string("\x41", 1),
                std::string("UNICODE\0", 8) + std::string("\xff\xfe\0\xd8", 4),
                std::string("ASCII\0\0\0", 8) + std::string("\xff", 1),
                std::string("JIS\0\0\0\0\0", 8) + "unknown",
                std::string(8, '\0') + "unknown" };
        for (const auto& value : bad) {
            MetaStore store;
            native(store, 0x9286, bytes(store, value));
            xmp(store, "UserComment[@xml:lang=x-default]", "source");
            store.finalize();
            const auto xml = packet(
                store, true,
                XmpExistingStandardNamespacePolicy::CanonicalizeManaged);
            EXPECT_NE(xml.find(">source<"), std::string::npos);
        }
    }

    TEST(MetadataExifText,
         VersionDowngradeRetainsExplicitBomsButRejectsExif3Text)
    {
        for (const bool modern : { false, true }) {
            MetaStore store;
            native(store, 0x9000, bytes(store, "0300"));
            native(store, 0x9286,
                   bytes(store, std::string("UNICODE\0", 8)
                                    + std::string("\xff\xfe"
                                                  "A\0",
                                                  4)));
            if (modern)
                native(store, 0xa436,
                       make_text(store.arena(), "title", TextEncoding::Ascii));
            xmp(store, "ExifVersion", "0232");
            store.finalize();
            if (modern)
                expect_failure(store,
                               MetadataCaptureTranslationStatus::NativeConflict,
                               { .conflict_policy = kReplace });
            else {
                ASSERT_EQ(translate_xmp_exif_text_metadata(
                              store, { .conflict_policy = kReplace }, &store)
                              .status,
                          kOk);
                EXPECT_NE(packet(store).find(">A<"), std::string::npos);
            }
        }
    }

    TEST(MetadataExifText, LegacyTwoPartCopyrightRemainsValid)
    {
        MetaStore store;
        const std::string credit("Photographer\0Editor", 19U);
        native(store, 0x8298,
               make_text(store.arena(), credit, TextEncoding::Ascii), "ifd0");
        store.finalize();
        EXPECT_TRUE(validate_store(store).ok());
        const auto measured = serialize_exif_tiff(store, {});
        ASSERT_EQ(measured.status, ExifTiffSerializeStatus::OutputTruncated);
        std::vector<std::byte> output(measured.needed);
        ASSERT_TRUE(serialize_exif_tiff(store, output).ok());
        MetaStore reread;
        ASSERT_EQ(decode_exif_tiff(output, reread, {}, {}).status,
                  ExifDecodeStatus::Ok);
        reread.finalize();
        const auto ids = reread.find_all(
            make_exif_tag_key_view("ifd0", 0x8298));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = reread.entry(ids[0]);
        EXPECT_EQ(e.origin.wire_type.code, 2U);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        const auto bytes = reread.arena().span(e.value.data.span);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(bytes.data()),
                              bytes.size()),
                  credit + std::string(1U, '\0'));
    }
}  // namespace
}  // namespace openmeta
