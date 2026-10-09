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
    constexpr auto kPreserve
        = MetadataCaptureTranslationConflictPolicy::PreserveExisting;
    constexpr auto kFail
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    constexpr auto kOk = MetadataCaptureTranslationStatus::Ok;
    constexpr std::array<std::string_view, 13> kFieldNames = {
        "ExifVersion", "FlashpixVersion", "UserComment", "ImageTitle",
        "Photographer", "ImageEditor", "CameraFirmware",
        "RAWDevelopingSoftware", "ImageEditingSoftware",
        "MetadataEditingSoftware", "CameraOwnerName", "LensMake", "LensModel"
    };
    constexpr std::array<uint16_t, 13> kFieldTags = {
        0x9000U, 0xa000U, 0x9286U, 0xa436U, 0xa437U, 0xa438U, 0xa439U,
        0xa43aU, 0xa43bU, 0xa43cU, 0xa430U, 0xa433U, 0xa434U
    };

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
    void native_wire(MetaStore& store, uint16_t tag, const MetaValue& value,
                     uint16_t wire_type, uint32_t wire_count,
                     std::string_view ifd = "exififd",
                     EntryFlags flags = EntryFlags::None)
    {
        Entry e;
        e.key = make_exif_tag_key(store.arena(), ifd, tag);
        e.value = value;
        e.origin.block = 7U;
        e.origin.order_in_block = 9U;
        e.origin.wire_type = { WireFamily::Tiff, wire_type };
        e.origin.wire_count = wire_count;
        e.origin.wire_type_name = store.arena().append_string("retained-type");
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
    const Entry* stored_entry(const MetaStore& store, uint16_t tag)
    {
        for (const Entry& e : store.entries()) {
            if (e.key.kind != MetaKeyKind::ExifTag
                || e.key.data.exif_tag.tag != tag)
                continue;
            const auto ifd = store.arena().span(e.key.data.exif_tag.ifd);
            if (std::string_view(reinterpret_cast<const char*>(ifd.data()),
                                 ifd.size()) == "exififd")
                return &e;
        }
        return nullptr;
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
    void expect_failure_distinct(
        MetaStore& source, MetadataCaptureTranslationStatus expected,
        MetadataExifTextTranslationOptions options = {})
    {
        source.finalize();
        const auto* source_entries = source.entries().data();
        const auto source_data = source.arena().bytes();
        const std::vector<std::byte> source_before(source_data.begin(),
                                                   source_data.end());
        MetaStore output;
        native(output, 0x010eU,
               make_text(output.arena(), "sentinel", TextEncoding::Ascii),
               "ifd0");
        output.finalize();
        const auto* output_entries = output.entries().data();
        const auto output_data = output.arena().bytes();
        const std::vector<std::byte> output_before(output_data.begin(),
                                                   output_data.end());
        EXPECT_EQ(translate_xmp_exif_text_metadata(source, options, &output)
                      .status,
                  expected);
        EXPECT_EQ(source.entries().data(), source_entries);
        EXPECT_EQ(std::vector<std::byte>(source.arena().bytes().begin(),
                                         source.arena().bytes().end()),
                  source_before);
        EXPECT_EQ(output.entries().data(), output_entries);
        EXPECT_EQ(std::vector<std::byte>(output.arena().bytes().begin(),
                                         output.arena().bytes().end()),
                  output_before);
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

    TEST(MetadataExifText, CleanExactNativeBatchPromotesWithoutReencoding)
    {
        MetaStore store;
        xmp(store, "ExifVersion", "0300");
        xmp(store, "FlashpixVersion", "0100");
        xmp(store, "UserComment", "A");
        for (size_t i = 3U; i < kFieldNames.size(); ++i)
            xmp(store, kFieldNames[i], "same", true);

        native_wire(store, 0x9000U, bytes(store, "0300"), 7U, 4U);
        native_wire(store, 0xa000U, bytes(store, "0100"), 7U, 4U);
        const std::string comment
            = std::string("UNICODE\0", 8U) + std::string("\xfe\xff", 2U)
              + std::string("\0A\0\0", 4U);
        native_wire(store, 0x9286U, bytes(store, comment), 7U,
                    static_cast<uint32_t>(comment.size()), "exififd",
                    EntryFlags::ValueBigEndian);
        for (size_t i = 3U; i < kFieldTags.size(); ++i) {
            const bool type129 = i == 3U || i >= 10U;
            const std::string raw("same\0", 5U);
            const MetaValue value
                = make_text(store.arena(), raw,
                            type129 ? TextEncoding::Utf8 : TextEncoding::Ascii);
            native_wire(store, kFieldTags[i], value, type129 ? 129U : 2U,
                        static_cast<uint32_t>(raw.size()));
        }
        native_wire(store, 0x013bU,
                    make_text(store.arena(), "Artist", TextEncoding::Ascii),
                    2U, 7U, "ifd0");
        native_wire(store, 0x0131U,
                    make_text(store.arena(), "Software", TextEncoding::Ascii),
                    2U, 9U, "ifd0");
        store.finalize();

        std::array<std::string, 13> before_raw;
        std::array<TextEncoding, 13> before_encoding {};
        std::array<uint16_t, 13> before_wire {};
        std::array<uint32_t, 13> before_wire_count {};
        std::array<uint32_t, 13> before_origin_order {};
        for (size_t i = 0U; i < kFieldTags.size(); ++i) {
            const Entry* entry = find(store, kFieldTags[i]);
            ASSERT_NE(entry, nullptr);
            const auto data = store.arena().span(entry->value.data.span);
            before_raw[i] = { reinterpret_cast<const char*>(data.data()),
                              data.size() };
            before_encoding[i] = entry->value.text_encoding;
            before_wire[i] = entry->origin.wire_type.code;
            before_wire_count[i] = entry->origin.wire_count;
            before_origin_order[i] = entry->origin.order_in_block;
        }

        const auto result = translate_xmp_exif_text_metadata(store, {}, &store);
        ASSERT_EQ(result.status, kOk);
        EXPECT_EQ(result.groups_translated, 13U);
        EXPECT_EQ(result.groups_unchanged, 0U);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.entries_updated, 13U);
        EXPECT_EQ(result.entries_removed, 0U);
        for (size_t i = 0U; i < kFieldTags.size(); ++i) {
            const Entry* entry = find(store, kFieldTags[i]);
            ASSERT_NE(entry, nullptr);
            const auto data = store.arena().span(entry->value.data.span);
            EXPECT_EQ(std::string(reinterpret_cast<const char*>(data.data()),
                                  data.size()),
                      before_raw[i]);
            EXPECT_EQ(entry->value.text_encoding, before_encoding[i]);
            EXPECT_EQ(entry->value.kind,
                      i < 3U ? MetaValueKind::Bytes : MetaValueKind::Text);
            EXPECT_EQ(entry->origin.wire_type.code, before_wire[i]);
            EXPECT_EQ(entry->origin.wire_type.family, WireFamily::Tiff);
            EXPECT_EQ(entry->origin.wire_count, before_wire_count[i]);
            EXPECT_EQ(entry->origin.order_in_block, before_origin_order[i]);
            EXPECT_EQ(entry->origin.block, 7U);
            EXPECT_EQ(std::string(
                          reinterpret_cast<const char*>(store.arena()
                                                            .span(entry->origin
                                                                      .wire_type_name)
                                                            .data()),
                          store.arena().span(entry->origin.wire_type_name)
                              .size()),
                      "retained-type");
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty));
            if (i == 2U)
                EXPECT_TRUE(any(entry->flags, EntryFlags::ValueBigEndian));
        }
        const auto repeated = translate_xmp_exif_text_metadata(store, {}, &store);
        EXPECT_EQ(repeated.status, kOk);
        EXPECT_EQ(repeated.groups_unchanged, 13U);
        EXPECT_EQ(repeated.entries_updated, 0U);
        EXPECT_EQ(repeated.entries_added, 0U);
    }

    TEST(MetadataExifText, CompleteAbsentDeletionCreatesTypedMarkersForAllFields)
    {
        for (const auto policy : { kPreserve, kFail, kReplace }) {
            MetaStore store;
            for (size_t i = 0U; i < kFieldNames.size(); ++i)
                xmp(store, kFieldNames[i], "", i >= 3U,
                    EntryFlags::Dirty | EntryFlags::Deleted);
            store.finalize();

            const auto result = translate_xmp_exif_text_metadata(
                store, { .conflict_policy = policy }, &store);
            ASSERT_EQ(result.status, kOk);
            EXPECT_EQ(result.source_properties, 13U);
            EXPECT_EQ(result.groups_translated, 13U);
            EXPECT_EQ(result.entries_added, 13U);
            EXPECT_EQ(result.entries_updated, 0U);
            EXPECT_EQ(result.entries_removed, 0U);
            for (size_t i = 0U; i < kFieldTags.size(); ++i) {
                EXPECT_EQ(find(store, kFieldTags[i]), nullptr);
                const Entry* entry = stored_entry(store, kFieldTags[i]);
                ASSERT_NE(entry, nullptr);
                EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty));
                EXPECT_TRUE(any(entry->flags, EntryFlags::Deleted));
                EXPECT_EQ(entry->origin.wire_type.family, WireFamily::Tiff);
                if (i < 3U) {
                    EXPECT_EQ(entry->value.kind, MetaValueKind::Bytes);
                    EXPECT_EQ(entry->value.elem_type, MetaElementType::U8);
                    EXPECT_EQ(entry->origin.wire_type.code, 7U);
                    EXPECT_EQ(entry->origin.wire_count, 0U);
                } else {
                    EXPECT_EQ(entry->value.kind, MetaValueKind::Text);
                    EXPECT_EQ(entry->value.text_encoding, TextEncoding::Utf8);
                    EXPECT_EQ(entry->origin.wire_type.code, 2U);
                    EXPECT_EQ(entry->origin.wire_count, 1U);
                }
            }
        }
    }

    TEST(MetadataExifText, CompleteDeletionPromotesOrReusesCleanNativeMarker)
    {
        for (const auto policy : { kPreserve, kFail, kReplace }) {
            MetaStore store;
            const std::string retained("clean-marker");
            native_wire(store, 0xa436U,
                        make_text(store.arena(), retained, TextEncoding::Ascii),
                        2U, static_cast<uint32_t>(retained.size() + 1U),
                        "exififd", EntryFlags::Deleted);
            xmp(store, "ImageTitle", "", true,
                EntryFlags::Dirty | EntryFlags::Deleted);
            store.finalize();

            const auto result = translate_xmp_exif_text_metadata(
                store, { .conflict_policy = policy }, &store);
            ASSERT_EQ(result.status, kOk);
            EXPECT_EQ(result.groups_translated, 1U);
            EXPECT_EQ(result.entries_updated, 1U);
            EXPECT_EQ(result.entries_added, 0U);
            EXPECT_EQ(result.entries_removed, 0U);
            EXPECT_EQ(find(store, 0xa436U), nullptr);
            const Entry* entry = stored_entry(store, 0xa436U);
            ASSERT_NE(entry, nullptr);
            EXPECT_EQ(entry->value.kind, MetaValueKind::Text);
            const auto value = store.arena().span(entry->value.data.span);
            EXPECT_EQ(std::string(reinterpret_cast<const char*>(value.data()),
                                  value.size()),
                      retained);
            EXPECT_EQ(entry->origin.wire_type.code, 2U);
            EXPECT_EQ(entry->origin.wire_count, retained.size() + 1U);
            EXPECT_EQ(entry->origin.order_in_block, 9U);
            EXPECT_EQ(std::string(
                          reinterpret_cast<const char*>(store.arena()
                                                            .span(entry->origin
                                                                      .wire_type_name)
                                                            .data()),
                          store.arena().span(entry->origin.wire_type_name)
                              .size()),
                      "retained-type");
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty));
            EXPECT_TRUE(any(entry->flags, EntryFlags::Deleted));
        }

        MetaStore already_deleted;
        native_wire(already_deleted, 0xa436U,
                    make_text(already_deleted.arena(), "dirty-marker",
                              TextEncoding::Ascii),
                    2U, 13U, "exififd",
                    EntryFlags::Dirty | EntryFlags::Deleted);
        xmp(already_deleted, "ImageTitle", "", true,
            EntryFlags::Dirty | EntryFlags::Deleted);
        already_deleted.finalize();
        const auto repeated = translate_xmp_exif_text_metadata(
            already_deleted, {}, &already_deleted);
        EXPECT_EQ(repeated.status, kOk);
        EXPECT_EQ(repeated.groups_unchanged, 1U);
        EXPECT_EQ(repeated.groups_translated, 0U);
        EXPECT_EQ(repeated.entries_added, 0U);
        EXPECT_EQ(repeated.entries_updated, 0U);
        EXPECT_EQ(repeated.entries_removed, 0U);
        const Entry* marker = stored_entry(already_deleted, 0xa436U);
        ASSERT_NE(marker, nullptr);
        EXPECT_TRUE(any(marker->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(marker->flags, EntryFlags::Deleted));
    }

    TEST(MetadataExifText, Type129AsciiEqualityUsesTheDeclaredExifVersion)
    {
        for (const EntryFlags flags : { EntryFlags::None, EntryFlags::Dirty }) {
            MetaStore store;
            native_wire(store, 0x9000U, bytes(store, "0300"), 7U, 4U);
            const std::string original("same\0", 5U);
            native_wire(store, 0xa436U,
                        make_text(store.arena(), original, TextEncoding::Utf8),
                        129U, static_cast<uint32_t>(original.size()), "exififd",
                        flags);
            xmp(store, "ImageTitle", "same", true);
            store.finalize();
            const auto result = translate_xmp_exif_text_metadata(
                store, { .conflict_policy = kFail }, &store);
            ASSERT_EQ(result.status, kOk);
            const Entry* entry = find(store, 0xa436U);
            ASSERT_NE(entry, nullptr);
            const auto raw_value = store.arena().span(entry->value.data.span);
            EXPECT_EQ(std::string(
                          reinterpret_cast<const char*>(raw_value.data()),
                          raw_value.size()),
                      original);
            EXPECT_EQ(entry->origin.wire_type.code, 129U);
            EXPECT_EQ(entry->value.text_encoding, TextEncoding::Utf8);
            EXPECT_TRUE(any(entry->flags, EntryFlags::Dirty));
            if (flags == EntryFlags::None) {
                EXPECT_EQ(result.entries_updated, 1U);
                EXPECT_EQ(result.groups_translated, 1U);
            } else {
                EXPECT_EQ(result.entries_updated, 0U);
                EXPECT_EQ(result.groups_unchanged, 1U);
            }
        }

        MetaStore legacy;
        native_wire(legacy, 0x9000U, bytes(legacy, "0232"), 7U, 4U);
        const std::string legacy_raw("owner\0", 6U);
        native_wire(legacy, 0xa430U,
                    make_text(legacy.arena(), legacy_raw, TextEncoding::Utf8),
                    129U, static_cast<uint32_t>(legacy_raw.size()), "exififd",
                    EntryFlags::Dirty);
        xmp(legacy, "CameraOwnerName", "owner", true);
        expect_failure(legacy,
                       MetadataCaptureTranslationStatus::NativeConflict,
                       { .conflict_policy = kFail });
    }

    TEST(MetadataExifText, ExplicitOwnerLensTextReplacementCanDowngradeType129)
    {
        for (size_t i = 10U; i < kFieldNames.size(); ++i) {
            for (const EntryFlags flags : { EntryFlags::None,
                                            EntryFlags::Dirty }) {
                for (const bool changed : { false, true }) {
                    MetaStore store;
                    native_wire(store, 0x9000U, bytes(store, "0300"), 7U, 4U);
                    const std::string original("same\0", 5U);
                    native_wire(
                        store, kFieldTags[i],
                        make_text(store.arena(), original, TextEncoding::Utf8),
                        129U, static_cast<uint32_t>(original.size()), "exififd",
                        flags);
                    xmp(store, "ExifVersion", "0232");
                    xmp(store, kFieldNames[i], changed ? "replacement" : "same",
                        true);
                    store.finalize();

                    const std::string_view expected
                        = changed ? "replacement" : "same";
                    const auto result = translate_xmp_exif_text_metadata(
                        store, { .conflict_policy = kReplace }, &store);
                    ASSERT_EQ(result.status, kOk);
                    EXPECT_EQ(result.entries_updated, 2U);
                    const Entry* entry = find(store, kFieldTags[i]);
                    ASSERT_NE(entry, nullptr);
                    const auto raw_value
                        = store.arena().span(entry->value.data.span);
                    EXPECT_EQ(std::string(
                                  reinterpret_cast<const char*>(raw_value.data()),
                                  raw_value.size()),
                              expected);
                    EXPECT_EQ(entry->value.text_encoding, TextEncoding::Ascii);
                    EXPECT_EQ(entry->origin.wire_type.code, 2U);
                    EXPECT_EQ(entry->origin.wire_count,
                              expected.size() + 1U);
                }
            }

            for (const EntryFlags flags : { EntryFlags::None,
                                            EntryFlags::Dirty }) {
                MetaStore omitted;
                native_wire(omitted, 0x9000U, bytes(omitted, "0300"), 7U, 4U);
                const std::string original("same\0", 5U);
                native_wire(
                    omitted, kFieldTags[i],
                    make_text(omitted.arena(), original, TextEncoding::Utf8),
                    129U, static_cast<uint32_t>(original.size()), "exififd",
                    flags);
                xmp(omitted, "ExifVersion", "0232");
                expect_failure(omitted,
                               MetadataCaptureTranslationStatus::NativeConflict,
                               { .conflict_policy = kReplace });
            }

            for (const auto policy : { kPreserve, kFail }) {
                for (const EntryFlags flags : { EntryFlags::None,
                                                EntryFlags::Dirty }) {
                    MetaStore retained;
                    native_wire(retained, 0x9000U, bytes(retained, "0300"),
                                7U, 4U);
                    const std::string original("same\0", 5U);
                    native_wire(
                        retained, kFieldTags[i],
                        make_text(retained.arena(), original,
                                  TextEncoding::Utf8),
                        129U, static_cast<uint32_t>(original.size()), "exififd",
                        flags);
                    xmp(retained, "ExifVersion", "0232");
                    xmp(retained, kFieldNames[i], "same", true);
                    if (policy == kPreserve) {
                        retained.finalize();
                        const auto result = translate_xmp_exif_text_metadata(
                            retained, { .conflict_policy = policy }, &retained);
                        ASSERT_EQ(result.status, kOk);
                        EXPECT_EQ(result.groups_preserved, 2U);
                        EXPECT_EQ(result.groups_translated, 0U);
                        EXPECT_EQ(result.entries_added, 0U);
                        EXPECT_EQ(result.entries_updated, 0U);
                        EXPECT_EQ(result.entries_removed, 0U);
                        EXPECT_EQ(raw(retained, 0x9000U), "0300");
                        const Entry* owner = find(retained, kFieldTags[i]);
                        ASSERT_NE(owner, nullptr);
                        const auto owner_raw
                            = retained.arena().span(owner->value.data.span);
                        EXPECT_EQ(std::string(
                                      reinterpret_cast<const char*>(
                                          owner_raw.data()),
                                      owner_raw.size()),
                                  original);
                        EXPECT_EQ(owner->origin.wire_type.code, 129U);
                    } else {
                        expect_failure(
                            retained,
                            MetadataCaptureTranslationStatus::NativeConflict,
                            { .conflict_policy = policy });
                    }
                }
            }
        }
    }

    TEST(MetadataExifText, SameValueOwnerLensPromotionKeepsType129WithoutDowngrade)
    {
        for (size_t i = 10U; i < kFieldNames.size(); ++i) {
            for (const EntryFlags flags : { EntryFlags::None,
                                            EntryFlags::Dirty }) {
                MetaStore store;
                // native_wire() retains provenance in block 7.
                for (unsigned block = 0U; block < 8U; ++block)
                    (void)store.add_block(BlockInfo {});
                native_wire(store, 0x9000U, bytes(store, "0300"), 7U, 4U);
                const std::string original("same\0", 5U);
                native_wire(store, kFieldTags[i],
                            make_text(store.arena(), original,
                                      TextEncoding::Utf8),
                            129U, static_cast<uint32_t>(original.size()),
                            "exififd", flags);
                xmp(store, kFieldNames[i], "same", true);
                store.finalize();
                const auto result = translate_xmp_exif_text_metadata(
                    store, { .conflict_policy = kFail }, &store);
                ASSERT_EQ(result.status, kOk);
                const Entry* entry = find(store, kFieldTags[i]);
                ASSERT_NE(entry, nullptr);
                const auto raw_value = store.arena().span(entry->value.data.span);
                EXPECT_EQ(std::string(
                              reinterpret_cast<const char*>(raw_value.data()),
                              raw_value.size()),
                          original);
                EXPECT_EQ(entry->origin.wire_type.code, 129U);
                EXPECT_EQ(entry->value.text_encoding, TextEncoding::Utf8);
                EXPECT_TRUE(validate_store(store).ok());
                EXPECT_EQ(serialize_exif_tiff(store, {}).status,
                          ExifTiffSerializeStatus::OutputTruncated);
                if (flags == EntryFlags::None)
                    EXPECT_EQ(result.entries_updated, 1U);
                else
                    EXPECT_EQ(result.entries_updated, 0U);
            }
        }
    }

    TEST(MetadataExifText, EveryExif3TextTagRejectsAnExplicitLegacyDowngrade)
    {
        for (size_t i = 3U; i <= 9U; ++i) {
            for (const EntryFlags flags : { EntryFlags::None,
                                            EntryFlags::Dirty }) {
                MetaStore store;
                native_wire(store, 0x9000U, bytes(store, "0300"), 7U, 4U);
                const std::string original("same\0", 5U);
                native_wire(store, kFieldTags[i],
                            make_text(store.arena(), original,
                                      TextEncoding::Utf8),
                            129U, static_cast<uint32_t>(original.size()),
                            "exififd", flags);
                native_wire(
                    store, 0x013bU,
                    make_text(store.arena(), "Artist", TextEncoding::Ascii),
                    2U, 7U, "ifd0");
                native_wire(
                    store, 0x0131U,
                    make_text(store.arena(), "Software", TextEncoding::Ascii),
                    2U, 9U, "ifd0");
                xmp(store, "ExifVersion", "0232");
                xmp(store, kFieldNames[i], "same", true);
                expect_failure(store,
                               MetadataCaptureTranslationStatus::NativeConflict,
                               { .conflict_policy = kReplace });
            }
        }
    }

    TEST(MetadataExifText, DisabledAndDirtyOnlySourcesLeaveNativeValuesUntouched)
    {
        MetaStore disabled;
        const std::string retained("native");
        native_wire(disabled, 0xa436U,
                    make_text(disabled.arena(), retained, TextEncoding::Ascii),
                    2U, static_cast<uint32_t>(retained.size() + 1U));
        xmp(disabled, "ImageTitle", "replacement", true);
        xmp(disabled, "CameraOwnerName", "", true,
            EntryFlags::Dirty | EntryFlags::Deleted);
        disabled.finalize();
        const Entry* before = find(disabled, 0xa436U);
        ASSERT_NE(before, nullptr);
        const auto before_span = disabled.arena().span(before->value.data.span);
        const std::string before_raw(
            reinterpret_cast<const char*>(before_span.data()), before_span.size());

        const auto result = translate_xmp_exif_text_metadata(
            disabled,
            { .image_title_to_exif = false,
              .camera_owner_name_to_exif = false },
            &disabled);
        EXPECT_EQ(result.status, kOk);
        EXPECT_EQ(result.source_properties, 0U);
        EXPECT_EQ(result.groups_translated, 0U);
        EXPECT_EQ(result.entries_added, 0U);
        EXPECT_EQ(result.entries_updated, 0U);
        EXPECT_EQ(result.entries_removed, 0U);
        const Entry* after = find(disabled, 0xa436U);
        ASSERT_NE(after, nullptr);
        const auto after_span = disabled.arena().span(after->value.data.span);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(after_span.data()),
                              after_span.size()),
                  before_raw);
        EXPECT_FALSE(any(after->flags, EntryFlags::Dirty));
        EXPECT_EQ(stored_entry(disabled, 0xa430U), nullptr);

        MetaStore clean_source;
        native_wire(clean_source, 0xa436U,
                    make_text(clean_source.arena(), "native",
                              TextEncoding::Ascii),
                    2U, 7U);
        xmp(clean_source, "ImageTitle", "clean", true, EntryFlags::None);
        clean_source.finalize();
        const auto omitted = translate_xmp_exif_text_metadata(
            clean_source, {}, &clean_source);
        EXPECT_EQ(omitted.status, kOk);
        EXPECT_EQ(omitted.source_properties, 0U);
        EXPECT_EQ(omitted.entries_updated, 0U);
        EXPECT_EQ(raw(clean_source, 0xa436U), "native");
    }

    TEST(MetadataExifText, PromotionAndDeletionBudgetsRollbackAliasedAndDistinct)
    {
        MetaStore promotion_alias;
        native_wire(promotion_alias, 0x9000U,
                    bytes(promotion_alias, "0300"), 7U, 4U);
        native_wire(promotion_alias, 0xa436U,
                    make_text(promotion_alias.arena(), "same",
                              TextEncoding::Ascii),
                    2U, 5U);
        xmp(promotion_alias, "ImageTitle", "same", true);
        expect_failure(promotion_alias,
                       MetadataCaptureTranslationStatus::OperationLimitExceeded,
                       { .max_operations = 0U });

        MetaStore promotion_distinct;
        native_wire(promotion_distinct, 0x9000U,
                    bytes(promotion_distinct, "0300"), 7U, 4U);
        native_wire(promotion_distinct, 0xa436U,
                    make_text(promotion_distinct.arena(), "same",
                              TextEncoding::Ascii),
                    2U, 5U);
        xmp(promotion_distinct, "ImageTitle", "same", true);
        expect_failure_distinct(
            promotion_distinct,
            MetadataCaptureTranslationStatus::OperationLimitExceeded,
            { .max_operations = 0U });

        MetaStore marker_alias;
        native_wire(marker_alias, 0xa436U,
                    make_text(marker_alias.arena(), "clean-marker",
                              TextEncoding::Ascii),
                    2U, 13U, "exififd", EntryFlags::Deleted);
        xmp(marker_alias, "ImageTitle", "", true,
            EntryFlags::Dirty | EntryFlags::Deleted);
        expect_failure(marker_alias,
                       MetadataCaptureTranslationStatus::OperationLimitExceeded,
                       { .max_operations = 0U });

        MetaStore marker_distinct;
        native_wire(marker_distinct, 0xa436U,
                    make_text(marker_distinct.arena(), "clean-marker",
                              TextEncoding::Ascii),
                    2U, 13U, "exififd", EntryFlags::Deleted);
        xmp(marker_distinct, "ImageTitle", "", true,
            EntryFlags::Dirty | EntryFlags::Deleted);
        expect_failure_distinct(
            marker_distinct,
            MetadataCaptureTranslationStatus::OperationLimitExceeded,
            { .max_operations = 0U });

        MetaStore deletion_alias;
        xmp(deletion_alias, "ImageTitle", "", true,
            EntryFlags::Dirty | EntryFlags::Deleted);
        expect_failure(deletion_alias,
                       MetadataCaptureTranslationStatus::EntryLimitExceeded,
                       { .max_added_entries = 0U });

        MetaStore deletion_distinct;
        xmp(deletion_distinct, "ImageTitle", "", true,
            EntryFlags::Dirty | EntryFlags::Deleted);
        expect_failure_distinct(
            deletion_distinct,
            MetadataCaptureTranslationStatus::EntryLimitExceeded,
            { .max_added_entries = 0U });

        MetaStore deletion_ops;
        xmp(deletion_ops, "ImageTitle", "", true,
            EntryFlags::Dirty | EntryFlags::Deleted);
        expect_failure_distinct(
            deletion_ops,
            MetadataCaptureTranslationStatus::OperationLimitExceeded,
            { .max_operations = 0U });
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
        for (size_t i = 4U; i <= 9U; ++i) {
            MetaStore exact_native;
            native_wire(exact_native, 0x9000U,
                        bytes(exact_native, "0300"), 7U, 4U);
            native_wire(
                exact_native, kFieldTags[i],
                make_text(exact_native.arena(), "same", TextEncoding::Ascii),
                2U, 5U);
            xmp(exact_native, kFieldNames[i], "same", true);
            expect_failure(exact_native, S::IncompleteSource);
        }
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
        const Entry* deleted_comment = stored_entry(deletion, 0x9286U);
        ASSERT_NE(deleted_comment, nullptr);
        EXPECT_EQ(deleted_comment->value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(deleted_comment->value.elem_type, MetaElementType::U8);
        EXPECT_TRUE(any(deleted_comment->flags, EntryFlags::Dirty));
        EXPECT_TRUE(any(deleted_comment->flags, EntryFlags::Deleted));
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
