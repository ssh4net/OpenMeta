// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_translation.h"
#include "openmeta/validate.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    constexpr std::string_view kExifEx = "http://cipa.jp/exif/1.0/";
    using Status = MetadataCaptureTranslationStatus;

    void xmp(MetaStore& store, std::string_view path, std::string_view text,
             EntryFlags flags = EntryFlags::Dirty)
    {
        Entry entry;
        entry.key = make_xmp_property_key(store.arena(), kExifEx, path);
        entry.value = make_text(store.arena(), text, TextEncoding::Utf8);
        entry.flags = flags;
        (void)store.add_entry(entry);
    }

    void native(MetaStore& store, uint16_t tag, uint16_t value,
                EntryFlags flags = EntryFlags::None)
    {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), "exififd", tag);
        entry.value = make_u16(value);
        entry.origin.wire_type = { WireFamily::Tiff, 3U };
        entry.origin.wire_count = 1U;
        entry.flags = flags;
        (void)store.add_entry(entry);
    }

    const Entry* find(const MetaStore& store, uint16_t tag)
    {
        const auto ids = store.find_all(make_exif_tag_key_view("exififd", tag));
        return ids.size() == 1U ? &store.entry(ids.front()) : nullptr;
    }

    std::string text_value(const MetaStore& store, uint16_t tag)
    {
        const Entry* entry = find(store, tag);
        if (!entry || entry->value.kind != MetaValueKind::Text) {
            return {};
        }
        const auto raw = store.arena().span(entry->value.data.span);
        return { reinterpret_cast<const char*>(raw.data()), raw.size() };
    }

    std::string portable(const MetaStore& store)
    {
        std::vector<std::byte> bytes(256U * 1024U);
        const XmpDumpResult result = dump_xmp_portable(store, bytes, {});
        EXPECT_EQ(result.status, XmpDumpStatus::Ok);
        return { reinterpret_cast<const char*>(bytes.data()),
                 static_cast<size_t>(result.written) };
    }

    TEST(MetadataDevelopmentTranslation, FullBatchStrictWirePortableAndRoundTrip)
    {
        MetaStore source;
        xmp(source, "DevelopmentType/DevelopmentCharacterstic", "1");
        xmp(source, "DevelopmentType/FactoryDefault", "4");
        xmp(source, "DevelopmentTypeDescription", "Developed by host");
        xmp(source, "DistortionCorrection", "1");
        xmp(source, "ChromaticAberrationCorrection", "0");
        xmp(source, "ShadingCorrection", "1");
        xmp(source, "NoiseReduction", "3");
        source.finalize();

        MetadataDevelopmentCorrectionTranslationOptions options;
        options.exif_version = 310U;
        const auto result = translate_xmp_development_correction_metadata(
            source, options, &source);
        ASSERT_EQ(result.status, Status::Ok);
        EXPECT_EQ(result.entries_added, 6U);
        EXPECT_EQ(result.source_properties, 7U);
        ASSERT_NE(find(source, 0xa40dU), nullptr);
        ASSERT_NE(find(source, 0xa40fU), nullptr);
        ASSERT_NE(find(source, 0xa410U), nullptr);
        ASSERT_NE(find(source, 0xa411U), nullptr);
        ASSERT_NE(find(source, 0xa412U), nullptr);
        ASSERT_NE(find(source, 0xa40eU), nullptr);
        EXPECT_EQ(find(source, 0xa40dU)->value.data.u64, 0x0104U);
        EXPECT_EQ(find(source, 0xa40fU)->value.data.u64, 1U);
        EXPECT_EQ(find(source, 0xa410U)->value.data.u64, 0U);
        EXPECT_EQ(find(source, 0xa411U)->value.data.u64, 1U);
        EXPECT_EQ(find(source, 0xa412U)->value.data.u64, 3U);
        EXPECT_EQ(find(source, 0xa40eU)->origin.wire_type.code, 129U);
        EXPECT_EQ(find(source, 0xa40eU)->origin.wire_count, 18U);
        EXPECT_EQ(text_value(source, 0xa40eU), "Developed by host");
        EXPECT_TRUE(validate_store(source).ok());

        const std::string packet = portable(source);
        EXPECT_NE(packet.find("xmlns:exifEX=\"http://cipa.jp/exif/1.0/\""),
                  std::string::npos);
        EXPECT_NE(packet.find(
                      "<exifEX:DevelopmentType rdf:parseType=\"Resource\">"),
                  std::string::npos);
        EXPECT_NE(packet.find("<exifEX:DevelopmentCharacterstic>1</exifEX:DevelopmentCharacterstic>"),
                  std::string::npos);
        EXPECT_NE(packet.find("<exifEX:FactoryDefault>4</exifEX:FactoryDefault>"),
                  std::string::npos);
        EXPECT_NE(packet.find("<exifEX:DevelopmentTypeDescription>Developed by host</exifEX:DevelopmentTypeDescription>"),
                  std::string::npos);

        MetaStore decoded;
        ASSERT_EQ(decode_xmp_packet(
                      std::as_bytes(std::span(packet.data(), packet.size())),
                      decoded)
                      .status,
                  XmpDecodeStatus::Ok);
        decoded.finalize();
        MetadataDevelopmentCorrectionTranslationOptions round_trip;
        round_trip.exif_version = 300U;
        round_trip.source_mode = MetadataCaptureTranslationSourceMode::All;
        const auto restored = translate_xmp_development_correction_metadata(
            decoded, round_trip, &decoded);
        ASSERT_EQ(restored.status, Status::Ok);
        const auto restored_type = decoded.find_all(
            make_exif_tag_key_view("exififd", 0xa40dU));
        ASSERT_EQ(restored_type.size(), 1U);
        EXPECT_EQ(decoded.entry(restored_type.front()).value.data.u64,
                   0x0104U);
        EXPECT_EQ(text_value(decoded, 0xa40eU), "Developed by host");
    }

    TEST(MetadataDevelopmentTranslation, VersionPolicyConflictAndShapeRollback)
    {
        MetaStore missing_version;
        xmp(missing_version, "DistortionCorrection", "1");
        missing_version.finalize();
        const auto missing = translate_xmp_development_correction_metadata(
            missing_version, {}, &missing_version);
        EXPECT_EQ(missing.status, Status::IncompleteSource);
        EXPECT_EQ(missing_version.find_all(
                      make_exif_tag_key_view("exififd", 0xa40fU))
                      .size(),
                  0U);

        MetaStore conflict;
        native(conflict, 0xa40fU, 0U);
        xmp(conflict, "DistortionCorrection", "1");
        conflict.finalize();
        MetadataDevelopmentCorrectionTranslationOptions strict;
        strict.exif_version = 310U;
        const auto failed = translate_xmp_development_correction_metadata(
            conflict, strict, &conflict);
        EXPECT_EQ(failed.status, Status::NativeConflict);
        EXPECT_EQ(find(conflict, 0xa40fU)->value.data.u64, 0U);

        MetaStore malformed;
        xmp(malformed, "DevelopmentType/DevelopmentCharacterstic", "1");
        xmp(malformed, "DevelopmentType/FactoryDefault", "8");
        malformed.finalize();
        const auto bad = translate_xmp_development_correction_metadata(
            malformed, strict, &malformed);
        EXPECT_EQ(bad.status, Status::ValueOutOfRange);
        EXPECT_EQ(malformed.find_all(
                      make_exif_tag_key_view("exififd", 0xa40dU))
                      .size(),
                  0U);

        MetaStore invalid_description;
        Entry description;
        description.key = make_exif_tag_key(invalid_description.arena(),
                                             "exififd", 0xa40eU);
        const std::string invalid_xml(1U, '\x01');
        description.value = make_text(invalid_description.arena(), invalid_xml,
                                       TextEncoding::Utf8);
        description.origin.wire_type = { WireFamily::Tiff, 129U };
        description.origin.wire_count = 2U;
        ASSERT_NE(invalid_description.add_entry(description),
                  kInvalidEntryId);
        invalid_description.finalize();
        EXPECT_FALSE(validate_store(invalid_description).ok());
    }

    TEST(MetadataDevelopmentTranslation, ReplaceAndTombstoneAreAtomic)
    {
        MetaStore replace;
        native(replace, 0xa40fU, 0U);
        xmp(replace, "DistortionCorrection", "1");
        replace.finalize();
        MetadataDevelopmentCorrectionTranslationOptions options;
        options.exif_version = 300U;
        options.conflict_policy
            = MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
        ASSERT_EQ(translate_xmp_development_correction_metadata(
                       replace, options, &replace)
                       .status,
                   Status::Ok);
        EXPECT_EQ(find(replace, 0xa40fU)->value.data.u64, 1U);

        MetaStore remove;
        native(remove, 0xa40fU, 1U);
        xmp(remove, "DistortionCorrection", "", EntryFlags::Dirty
                                            | EntryFlags::Deleted);
        remove.finalize();
        ASSERT_EQ(translate_xmp_development_correction_metadata(
                       remove, options, &remove)
                       .status,
                   Status::Ok);
        EXPECT_EQ(remove.find_all(
                      make_exif_tag_key_view("exififd", 0xa40fU))
                      .size(),
                  0U);
    }

}  // namespace
}  // namespace openmeta
