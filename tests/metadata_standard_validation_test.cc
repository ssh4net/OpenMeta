// SPDX-License-Identifier: Apache-2.0

#include "capture_sync_fixture.h"

#include "openmeta/exif_tiff_decode.h"
#include "openmeta/exif_tiff_serialize.h"
#include "openmeta/metadata_authoring.h"
#include "openmeta/metadata_editing.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/validate.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace openmeta {
namespace {

    static const std::array<URational, 4> kLens { URational { 24U, 1U },
                                                  { 70U, 1U },
                                                  { 0U, 0U },
                                                  { 0U, 0U } };
    static const std::array<URational, 3> kCoordinate { URational { 35U, 1U },
                                                        { 40U, 1U },
                                                        { 1U, 3U } };
    static constexpr std::array<uint16_t, 4> kSubject { 1U, 2U, 0U, 65535U };
    static constexpr char kGpsText[] = "ASCII\0\0\0GPS";

    static std::vector<MetadataAuthoringEntry> routed_fields()
    {
        std::vector<MetadataAuthoringEntry> entries;
        constexpr std::array<uint16_t, 16> shorts {
            0x8822U, 0x8830U, 0x9207U, 0x9208U, 0x9209U, 0xa210U,
            0xa217U, 0xa401U, 0xa402U, 0xa403U, 0xa406U, 0xa407U,
            0xa408U, 0xa409U, 0xa40aU, 0xa40cU
        };
        for (uint16_t tag : shorts)
            entries.push_back({ make_exif_tag_key_view("exififd", tag),
                                make_value_view_u16(1U) });
        for (uint16_t tag = 0x8831U; tag <= 0x8835U; ++tag)
            entries.push_back({ make_exif_tag_key_view("exififd", tag),
                                make_value_view_u32(400U) });
        for (uint16_t tag : { 0x9201U, 0x9203U })
            entries.push_back({ make_exif_tag_key_view("exififd", tag),
                                make_value_view_srational(-1, 7) });
        for (uint16_t tag : { 0x9202U, 0x9205U, 0x9206U, 0xa20bU, 0xa20eU,
                              0xa20fU, 0xa215U, 0xa404U })
            entries.push_back({ make_exif_tag_key_view("exififd", tag),
                                make_value_view_urational(1U, 3U) });
        for (uint16_t tag : { 0x8824U, 0xa431U, 0xa435U })
            entries.push_back(
                { make_exif_tag_key_view("exififd", tag),
                  make_value_view_text("camera", TextEncoding::Ascii) });
        for (uint16_t tag : { 0x9290U, 0x9291U, 0x9292U })
            entries.push_back({ make_exif_tag_key_view("exififd", tag),
                                make_value_view_text("1234567890123456789  ",
                                                     TextEncoding::Ascii) });
        for (uint16_t tag : { 0x9214U, 0xa214U }) {
            const uint32_t count = tag == 0x9214U ? 4U : 2U;
            entries.push_back(
                { make_exif_tag_key_view("exififd", tag),
                  make_value_view_array(
                      MetaElementType::U16,
                      std::as_bytes(std::span(kSubject)).first(count * 2U),
                      count) });
        }
        entries.push_back(
            { make_exif_tag_key_view("exififd", 0xa420U),
              make_value_view_text("00112233445566778899aAbBcCdDeEfF",
                                   TextEncoding::Ascii) });
        entries.push_back(
            { make_exif_tag_key_view("exififd", 0xa432U),
              make_value_view_array(MetaElementType::URational,
                                    std::as_bytes(std::span(kLens)), 4U) });
        for (uint16_t tag : { 0x0008U, 0x0012U })
            entries.push_back(
                { make_exif_tag_key_view("gpsifd", tag),
                  make_value_view_text("", TextEncoding::Ascii) });
        constexpr std::array<uint16_t, 9> ref_tags {
            0x0009U, 0x000aU, 0x000cU, 0x000eU, 0x0010U,
            0x0013U, 0x0015U, 0x0017U, 0x0019U
        };
        constexpr std::array<std::string_view, 9> refs { "V", "3", "K",
                                                         "T", "M", "N",
                                                         "W", "T", "N" };
        for (size_t i = 0U; i < refs.size(); ++i)
            entries.push_back(
                { make_exif_tag_key_view("gpsifd", ref_tags[i]),
                  make_value_view_text(refs[i], TextEncoding::Ascii) });
        for (uint16_t tag :
             { 0x000bU, 0x000dU, 0x000fU, 0x0011U, 0x0018U, 0x001aU, 0x001fU })
            entries.push_back({ make_exif_tag_key_view("gpsifd", tag),
                                make_value_view_urational(1U, 3U) });
        for (uint16_t tag : { 0x0014U, 0x0016U })
            entries.push_back(
                { make_exif_tag_key_view("gpsifd", tag),
                  make_value_view_array(MetaElementType::URational,
                                        std::as_bytes(std::span(kCoordinate)),
                                        3U) });
        for (uint16_t tag : { 0x001bU, 0x001cU })
            entries.push_back(
                { make_exif_tag_key_view("gpsifd", tag),
                  make_value_view_bytes(std::as_bytes(std::span(kGpsText))
                                            .first(sizeof(kGpsText) - 1U)) });
        entries.push_back({ make_exif_tag_key_view("gpsifd", 0x001eU),
                            make_value_view_u16(1U) });
        return entries;
    }

    static std::vector<std::byte> canonical(const MetaStore& store)
    {
        const ExifTiffSerializeResult measured = serialize_exif_tiff(store, {});
        EXPECT_EQ(measured.status, ExifTiffSerializeStatus::OutputTruncated);
        std::vector<std::byte> bytes(static_cast<size_t>(measured.needed));
        EXPECT_EQ(serialize_exif_tiff(store, bytes).status,
                  ExifTiffSerializeStatus::Ok);
        return bytes;
    }

    TEST(MetadataStandardValidation, All64RoutedFieldsAreKnownAndStandalone)
    {
        const std::vector<MetadataAuthoringEntry> fields = routed_fields();
        ASSERT_EQ(fields.size(), 64U);
        MetadataAuthoringOptions options;
        options.validation.unknown_exif_tags = MetadataUnknownTagPolicy::Error;
        MetaStore together;
        ASSERT_TRUE(create_metadata_store(fields, &together, options).ok());
        const std::vector<std::byte> bytes = canonical(together);
        MetaStore decoded;
        ASSERT_EQ(decode_exif_tiff(bytes, decoded, {}, {}).status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        EXPECT_TRUE(validate_store(decoded, options.validation).ok());
        for (const MetadataAuthoringEntry& field : fields) {
            SCOPED_TRACE(field.key.data.exif_tag.tag);
            MetaStore isolated;
            ASSERT_TRUE(
                create_metadata_store(std::span(&field, 1U), &isolated, options)
                    .ok());
            EXPECT_EQ(isolated.entries().size(), 1U);
            MetadataAuthoringEntry malformed = field;
            malformed.value = field.value.kind == MetaValueKind::Text
                                  ? make_value_view_u16(1U)
                                  : make_value_view_text("wrong type",
                                                         TextEncoding::Ascii);
            const std::vector<std::byte> before = canonical(isolated);
            const MetadataAuthoringResult result
                = create_metadata_store(std::span(&malformed, 1U), &isolated);
            EXPECT_EQ(result.status, MetadataAuthoringStatus::ValidationFailed);
            EXPECT_EQ(result.validation_issue,
                      MetadataValidationIssueCode::WrongType);
            EXPECT_EQ(canonical(isolated), before);
        }
    }

    TEST(MetadataStandardValidation,
         TypedEditsAndSerializationRejectMalformedBatchWithoutPublication)
    {
        const std::vector<MetadataAuthoringEntry> fields = routed_fields();
        MetaStore source;
        ASSERT_TRUE(create_metadata_store(fields, &source).ok());
        const std::vector<std::byte> before = canonical(source);
        for (const MetadataAuthoringEntry& field : fields) {
            SCOPED_TRACE(field.key.data.exif_tag.tag);
            std::array<MetadataTypedEditingOperation, 2> operations {};
            operations[0].kind        = MetadataEditingOperationKind::Set;
            operations[0].entry       = fields[0];
            operations[0].entry.value = make_value_view_u16(2U);
            operations[1].kind        = MetadataEditingOperationKind::Set;
            operations[1].entry       = field;
            operations[1].entry.value = make_value_view_u64(1U);
            EXPECT_FALSE(edit_metadata_typed(source, operations, &source).ok());
            EXPECT_EQ(canonical(source), before);

            MetadataAuthoringOptions unchecked;
            unchecked.validation.validate_schema = false;
            MetaStore invalid;
            ASSERT_TRUE(
                create_metadata_store(std::span(&operations[1].entry, 1U),
                                      &invalid, unchecked)
                    .ok());
            std::array<std::byte, 64> output;
            output.fill(std::byte { 0xa5 });
            EXPECT_EQ(serialize_exif_tiff(invalid, output).status,
                      ExifTiffSerializeStatus::InvalidMetadata);
            for (std::byte byte : output)
                EXPECT_EQ(byte, std::byte { 0xa5 });
        }
    }

    TEST(MetadataStandardValidation,
         InteropReusedIdsAndStructuralPointersRoundTrip)
    {
        constexpr std::array<std::byte, 4> version { std::byte { '0' },
                                                     std::byte { '1' },
                                                     std::byte { '0' },
                                                     std::byte { '0' } };
        const std::array fields {
            MetadataAuthoringEntry {
                make_exif_tag_key_view("interopifd", 1U),
                make_value_view_text("R98", TextEncoding::Ascii) },
            MetadataAuthoringEntry { make_exif_tag_key_view("interopifd", 2U),
                                     make_value_view_bytes(version) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("interopifd", 0x1000U),
                make_value_view_text("JPEG", TextEncoding::Ascii) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("interopifd", 0x1001U),
                make_value_view_u32(4000U) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("interopifd", 0x1002U),
                make_value_view_u16(3000U) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("gpsifd", 1U),
                make_value_view_text("N", TextEncoding::Ascii) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("gpsifd", 2U),
                make_value_view_array(MetaElementType::URational,
                                      std::as_bytes(std::span(kCoordinate)),
                                      3U) },
        };
        MetadataAuthoringOptions strict;
        strict.validation.unknown_exif_tags = MetadataUnknownTagPolicy::Error;
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(fields, &store, strict).ok());
        const std::vector<std::byte> bytes = canonical(store);
        MetaStore decoded;
        ASSERT_EQ(decode_exif_tiff(bytes, decoded, {}, {}).status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        EXPECT_TRUE(validate_store(decoded, strict.validation).ok());
        EXPECT_EQ(
            decoded.find_all(make_exif_tag_key_view("exififd", 0xa005U)).size(),
            1U);
        for (const MetadataAuthoringEntry& field : fields) {
            const std::array duplicate { field, field };
            const MetadataAuthoringResult result
                = create_metadata_store(duplicate, &store, strict);
            EXPECT_EQ(result.validation_issue,
                      MetadataValidationIssueCode::DuplicateSingleton);
        }
        MetadataAuthoringEntry bad { make_exif_tag_key_view("interopifd", 1U),
                                     make_value_view_u16(42U) };
        EXPECT_EQ(
            create_metadata_store(std::span(&bad, 1U), &store).validation_issue,
            MetadataValidationIssueCode::WrongType);
        bad.key = make_exif_tag_key_view("ifd0", 0x9207U);
        EXPECT_EQ(
            create_metadata_store(std::span(&bad, 1U), &store).validation_issue,
            MetadataValidationIssueCode::WrongIfd);
        bad.key = make_exif_tag_key_view("ifd0", 0xf001U);
        EXPECT_TRUE(create_metadata_store(std::span(&bad, 1U), &store).ok());
        EXPECT_EQ(create_metadata_store(std::span(&bad, 1U), &store, strict)
                      .validation_issue,
                  MetadataValidationIssueCode::UnknownExifTag);
        bad.key = make_exif_tag_key_view("vendor-private", 1U);
        EXPECT_TRUE(create_metadata_store(std::span(&bad, 1U), &store).ok());
    }

    TEST(MetadataStandardValidation,
         ValuesCountsHintsAndSentinelsKeepTheirNativeContracts)
    {
        MetaStore output;
        const std::vector<MetadataAuthoringEntry> fields = routed_fields();
        ASSERT_TRUE(create_metadata_store(fields, &output).ok());
        const std::vector<std::byte> before = canonical(output);
        constexpr std::array<std::array<uint16_t, 2>, 16> invalid_codes {
            { { 0x8822U, 9U },
              { 0x8830U, 8U },
              { 0x9207U, 7U },
              { 0x9208U, 8U },
              { 0x9209U, 2U },
              { 0xa217U, 6U },
              { 0xa401U, 2U },
              { 0xa402U, 3U },
              { 0xa403U, 2U },
              { 0xa406U, 4U },
              { 0xa407U, 5U },
              { 0xa408U, 3U },
              { 0xa409U, 3U },
              { 0xa40aU, 3U },
              { 0xa40cU, 4U },
              { 0xa210U, 6U } }
        };
        for (const std::array<uint16_t, 2>& code : invalid_codes) {
            SCOPED_TRACE(code[0]);
            const MetadataAuthoringEntry entry {
                make_exif_tag_key_view("exififd", code[0]),
                make_value_view_u16(code[1])
            };
            EXPECT_FALSE(
                create_metadata_store(std::span(&entry, 1U), &output).ok());
        }
        const std::array malformed {
            MetadataAuthoringEntry { make_exif_tag_key_view("exififd", 0x8831U),
                                     make_value_view_u32(0U) },
            MetadataAuthoringEntry { make_exif_tag_key_view("exififd", 0xa215U),
                                     make_value_view_urational(0U, 1U) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("exififd", 0x9290U),
                make_value_view_text("123x", TextEncoding::Ascii) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("exififd", 0xa431U),
                make_value_view_text("\xc3\xa9", TextEncoding::Utf8) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("exififd", 0xa420U),
                make_value_view_text("z0112233445566778899aAbBcCdDeEfF",
                                     TextEncoding::Ascii) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("gpsifd", 0x0009U),
                make_value_view_text("a", TextEncoding::Ascii) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("gpsifd", 0x0019U),
                make_value_view_text("T", TextEncoding::Ascii) },
            MetadataAuthoringEntry { make_exif_tag_key_view("gpsifd", 0x001eU),
                                     make_value_view_u16(2U) },
            MetadataAuthoringEntry { make_exif_tag_key_view("gpsifd", 0x0018U),
                                     make_value_view_urational(360U, 1U) },
            MetadataAuthoringEntry { make_exif_tag_key_view("gpsifd", 0x001fU),
                                     make_value_view_urational(1U, 0U) },
        };
        for (const MetadataAuthoringEntry& entry : malformed) {
            SCOPED_TRACE(entry.key.data.exif_tag.tag);
            EXPECT_FALSE(
                create_metadata_store(std::span(&entry, 1U), &output).ok());
        }
        EXPECT_EQ(canonical(output), before);
        const std::array bad_counts {
            MetadataAuthoringEntry {
                make_exif_tag_key_view("exififd", 0x9207U),
                make_value_view_array(
                    MetaElementType::U16,
                    std::as_bytes(std::span(kSubject)).first(4U), 2U) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("exififd", 0xa214U),
                make_value_view_array(MetaElementType::U16,
                                      std::as_bytes(std::span(kSubject)), 4U) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("gpsifd", 0x0014U),
                make_value_view_array(
                    MetaElementType::URational,
                    std::as_bytes(std::span(kCoordinate)).first(16U), 2U) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("gpsifd", 0x001bU),
                make_value_view_bytes(
                    std::as_bytes(std::span(kGpsText)).first(7U)) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("interopifd", 2U),
                make_value_view_bytes(
                    std::as_bytes(std::span(kGpsText)).first(5U)) },
        };
        for (const MetadataAuthoringEntry& entry : bad_counts) {
            SCOPED_TRACE(entry.key.data.exif_tag.tag);
            EXPECT_EQ(create_metadata_store(std::span(&entry, 1U), &output)
                          .validation_issue,
                      MetadataValidationIssueCode::WrongCount);
        }
        MetadataAuthoringEntry hinted = fields[0];
        hinted.wire_type              = { WireFamily::Tiff, 4U };
        EXPECT_EQ(create_metadata_store(std::span(&hinted, 1U), &output)
                      .validation_issue,
                  MetadataValidationIssueCode::InvalidWireType);
        hinted.wire_type  = {};
        hinted.wire_count = 2U;
        EXPECT_EQ(create_metadata_store(std::span(&hinted, 1U), &output)
                      .validation_issue,
                  MetadataValidationIssueCode::InvalidWireType);

        const std::array valid {
            MetadataAuthoringEntry { make_exif_tag_key_view("exififd", 0x9207U),
                                     make_value_view_u16(255U) },
            MetadataAuthoringEntry { make_exif_tag_key_view("exififd", 0x9208U),
                                     make_value_view_u16(255U) },
            MetadataAuthoringEntry { make_exif_tag_key_view("exififd", 0x9203U),
                                     make_value_view_srational(-1, 17) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("exififd", 0x9206U),
                make_value_view_urational(UINT32_MAX, 19U) },
            MetadataAuthoringEntry { make_exif_tag_key_view("exififd", 0xa404U),
                                     make_value_view_urational(0U, 9U) },
            MetadataAuthoringEntry { make_exif_tag_key_view("exififd", 0x9201U),
                                     make_value_view_srational(-1, -2) },
            MetadataAuthoringEntry { make_exif_tag_key_view("exififd", 0xa210U),
                                     make_value_view_u16(5U) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("exififd", 0xa431U),
                make_value_view_text("", TextEncoding::Ascii) },
        };
        ASSERT_TRUE(create_metadata_store(valid, &output).ok());
        const std::vector<std::byte> wire = canonical(output);
        MetaStore decoded;
        ASSERT_EQ(decode_exif_tiff(wire, decoded, {}, {}).status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        EXPECT_TRUE(validate_store(decoded).ok());
        const EntryId distance
            = decoded.find_all(make_exif_tag_key_view("exififd", 0x9206U))
                  .front();
        EXPECT_EQ(decoded.entry(distance).value.data.ur.numer, UINT32_MAX);
        EXPECT_EQ(decoded.entry(distance).value.data.ur.denom, 19U);
    }

    TEST(MetadataStandardValidation, GpsEncodedTextBoundsAndUnknownPayloads)
    {
        constexpr std::array<std::array<char, 8>, 4> headers {
            { { 'A', 'S', 'C', 'I', 'I', 0, 0, 0 },
              { 'J', 'I', 'S', 0, 0, 0, 0, 0 },
              { 'U', 'N', 'I', 'C', 'O', 'D', 'E', 0 },
              { 0, 0, 0, 0, 0, 0, 0, 0 } }
        };
        MetaStore output;
        for (uint16_t tag : { 0x001bU, 0x001cU }) {
            for (const std::array<char, 8>& header : headers) {
                const MetadataAuthoringEntry entry {
                    make_exif_tag_key_view("gpsifd", tag),
                    make_value_view_bytes(std::as_bytes(std::span(header)))
                };
                EXPECT_TRUE(
                    create_metadata_store(std::span(&entry, 1U), &output).ok());
            }
            std::array<std::byte, 9> malformed {};
            std::memcpy(malformed.data(), headers[0].data(), 8U);
            malformed.back() = std::byte { 0xff };
            MetadataAuthoringEntry entry { make_exif_tag_key_view("gpsifd", tag),
                                           make_value_view_bytes(malformed) };
            EXPECT_FALSE(
                create_metadata_store(std::span(&entry, 1U), &output).ok());
            malformed[0] = std::byte { '?' };
            EXPECT_FALSE(
                create_metadata_store(std::span(&entry, 1U), &output).ok());
        }
    }

    TEST(MetadataStandardValidation,
         InvalidNativeEnumRetainsExistingPortableProperty)
    {
        const std::array fields {
            MetadataAuthoringEntry { make_exif_tag_key_view("exififd", 0x9207U),
                                     make_value_view_u16(7U) },
            MetadataAuthoringEntry {
                make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                           "MeteringMode"),
                make_value_view_text("5", TextEncoding::Ascii) },
            MetadataAuthoringEntry {
                make_exif_tag_key_view("gpsifd", 0x0009U),
                make_value_view_text("x", TextEncoding::Ascii) },
            MetadataAuthoringEntry {
                make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                           "GPSStatus"),
                make_value_view_text("A", TextEncoding::Ascii) },
        };
        MetadataAuthoringOptions unchecked;
        unchecked.validation.validate_schema = false;
        MetaStore store;
        ASSERT_TRUE(create_metadata_store(fields, &store, unchecked).ok());
        std::vector<std::byte> output(4096U);
        XmpPortableOptions options;
        options.include_existing_xmp = true;
        const XmpDumpResult result = dump_xmp_portable(store, output, options);
        ASSERT_EQ(result.status, XmpDumpStatus::Ok);
        const std::string_view packet(reinterpret_cast<const char*>(
                                          output.data()),
                                      static_cast<size_t>(result.written));
        EXPECT_NE(packet.find("<exif:MeteringMode>5</exif:MeteringMode>"),
                  std::string_view::npos);
        EXPECT_EQ(packet.find("<exif:MeteringMode>7</exif:MeteringMode>"),
                  std::string_view::npos);
        EXPECT_NE(packet.find("<exif:GPSStatus>A</exif:GPSStatus>"),
                  std::string_view::npos);
    }

    TEST(MetadataStandardValidation,
         CombinedCaptureGpsTranslationSerializationAndSnapshot)
    {
        MetaStore store;
        ASSERT_EQ(decode_xmp_packet(
                      std::as_bytes(std::span(test::kCaptureSyncXml.data(),
                                              test::kCaptureSyncXml.size())),
                      store, {}, {})
                      .status,
                  XmpDecodeStatus::Ok);
        store.finalize();
        ASSERT_TRUE(test::capture_sync_translate(store));
        constexpr std::array<std::array<std::string_view, 2>, 26> properties {
            { { "GPSLatitude", "35,40,1N" },
              { "GPSLongitude", "139,45,2E" },
              { "GPSAltitude", "123/10" },
              { "GPSAltitudeRef", "0" },
              { "GPSTimeStamp", "2026-09-17T01:02:03.125Z" },
              { "GPSSpeedRef", "K" },
              { "GPSSpeed", "123/10" },
              { "GPSTrackRef", "T" },
              { "GPSTrack", "35999/100" },
              { "GPSImgDirectionRef", "M" },
              { "GPSImgDirection", "91/2" },
              { "GPSDestLatitude", "36,1,2S" },
              { "GPSDestLongitude", "140,2,3W" },
              { "GPSDestBearingRef", "T" },
              { "GPSDestBearing", "100/3" },
              { "GPSDestDistanceRef", "N" },
              { "GPSDestDistance", "1/3" },
              { "GPSStatus", "A" },
              { "GPSMeasureMode", "3" },
              { "GPSDOP", "1/3" },
              { "GPSDifferential", "1" },
              { "GPSHPositioningError", "1/3" },
              { "GPSSatellites", "1 2 3" },
              { "GPSMapDatum", "WGS-84" },
              { "GPSProcessingMethod", "GPS" },
              { "GPSAreaInformation", "area" } }
        };
        std::array<MetadataTypedEditingOperation, properties.size()> edits {};
        for (size_t i = 0U; i < properties.size(); ++i) {
            edits[i].kind = MetadataEditingOperationKind::Add;
            edits[i].entry.key
                = make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                             properties[i][0]);
            edits[i].entry.value = make_value_view_text(properties[i][1],
                                                        TextEncoding::Ascii);
        }
        ASSERT_TRUE(edit_metadata_typed(store, edits, &store).ok());
        // Quality supplies the explicit 2.3 version needed by horizontal error.
        ASSERT_EQ(translate_xmp_gps_quality_metadata(store, {}, &store).status,
                  MetadataGpsTranslationStatus::Ok);
        ASSERT_EQ(translate_xmp_gps_metadata(store, {}, &store).status,
                  MetadataGpsTranslationStatus::Ok);
        ASSERT_TRUE(
            translate_xmp_gps_navigation_metadata(store, {}, &store).status
            == MetadataGpsTranslationStatus::Ok);
        ASSERT_TRUE(
            translate_xmp_gps_destination_metadata(store, {}, &store).status
            == MetadataGpsTranslationStatus::Ok);
        ASSERT_EQ(translate_xmp_gps_text_metadata(store, {}, &store).status,
                  MetadataGpsTranslationStatus::Ok);
        MetadataValidationOptions strict;
        strict.unknown_exif_tags = MetadataUnknownTagPolicy::Error;
        EXPECT_TRUE(validate_store(store, strict).ok());
        for (uint16_t tag = 0U; tag <= 31U; ++tag)
            ASSERT_EQ(
                store.find_all(make_exif_tag_key_view("gpsifd", tag)).size(),
                1U);
        const std::vector<std::byte> wire = canonical(store);
        MetaStore decoded;
        ASSERT_EQ(decode_exif_tiff(wire, decoded, {}, {}).status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        EXPECT_TRUE(validate_store(decoded, strict).ok());
        test::capture_sync_expect_native(decoded, store);
        EXPECT_EQ(canonical(decoded), wire);
        const TransferSourceSnapshot snapshot = build_transfer_source_snapshot(
            store);
        std::vector<std::byte> serialized;
        ASSERT_TRUE(
            serialize_transfer_source_snapshot(snapshot, &serialized).status
            == TransferStatus::Ok);
        TransferSourceSnapshot restored;
        ASSERT_TRUE(
            deserialize_transfer_source_snapshot(serialized, &restored).status
            == TransferStatus::Ok);
        EXPECT_TRUE(validate_store(restored.store, strict).ok());
        EXPECT_EQ(canonical(restored.store), wire);
        std::vector<std::byte> packet(65536U);
        EXPECT_EQ(dump_xmp_portable(restored.store, packet, {}).status,
                  XmpDumpStatus::Ok);
    }

}  // namespace
}  // namespace openmeta
