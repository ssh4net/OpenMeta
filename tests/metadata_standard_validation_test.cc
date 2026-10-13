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
#include <string_view>
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

    struct TargetTestField final {
        std::string_view ifd;
        uint16_t tag = 0U;
    };

    struct TargetTestSetup final {
        TargetTestField omitted;
        TargetTestField flagged;
        EntryFlags flags = EntryFlags::None;
        TargetTestField wire_field;
        WireType wire_type;
        uint32_t wire_count = 0U;
    };

    static bool same_target_test_field(const TargetTestField& field,
                                       std::string_view ifd,
                                       uint16_t tag) noexcept
    {
        return !field.ifd.empty() && field.ifd == ifd && field.tag == tag;
    }

    static EntryId add_target_test_entry(MetaStore* store, std::string_view ifd,
                                         uint16_t tag, MetaValue value,
                                         EntryFlags flags    = EntryFlags::None,
                                         WireType wire_type  = {},
                                         uint32_t wire_count = 0U)
    {
        Entry entry;
        entry.key               = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value             = value;
        entry.flags             = flags;
        entry.origin.wire_type  = wire_type;
        entry.origin.wire_count = wire_count;
        return store->add_entry(entry);
    }

    static void add_target_profile_field(MetaStore* store,
                                         const TargetTestSetup& setup,
                                         std::string_view ifd, uint16_t tag,
                                         MetaValue value)
    {
        if (same_target_test_field(setup.omitted, ifd, tag)) {
            return;
        }
        const EntryFlags flags = same_target_test_field(setup.flagged, ifd, tag)
                                     ? setup.flags
                                     : EntryFlags::None;
        const bool has_wire    = same_target_test_field(setup.wire_field, ifd,
                                                        tag);
        add_target_test_entry(store, ifd, tag, value, flags,
                              has_wire ? setup.wire_type : WireType {},
                              has_wire ? setup.wire_count : 0U);
    }

    static MetaValue target_test_u16_array(MetaStore* store,
                                           std::span<const uint16_t> values)
    {
        return make_array(store->arena(), MetaElementType::U16,
                          std::as_bytes(values),
                          static_cast<uint32_t>(sizeof(uint16_t)));
    }

    static MetaValue target_test_u32_array(MetaStore* store,
                                           std::span<const uint32_t> values)
    {
        return make_array(store->arena(), MetaElementType::U32,
                          std::as_bytes(values),
                          static_cast<uint32_t>(sizeof(uint32_t)));
    }

    static void append_target_profile_entries(MetaStore* store,
                                              MetadataTargetProfile profile,
                                              const TargetTestSetup& setup = {})
    {
        static constexpr std::array<std::byte, 4> version { std::byte { '0' },
                                                            std::byte { '3' },
                                                            std::byte { '0' },
                                                            std::byte { '0' } };
        static constexpr std::array<std::byte, 4> components {
            std::byte { 1U }, std::byte { 2U }, std::byte { 3U },
            std::byte { 0U }
        };
        static constexpr std::array<uint16_t, 3> bits { 8U, 8U, 8U };
        static constexpr std::array<uint16_t, 2> subsampling { 2U, 2U };

        add_target_profile_field(store, setup, "exififd", 0x9000U,
                                 make_bytes(store->arena(), version));
        add_target_profile_field(store, setup, "exififd", 0xa001U,
                                 make_u16(1U));
        if (profile == MetadataTargetProfile::ExifJpeg) {
            add_target_profile_field(store, setup, "exififd", 0xa002U,
                                     make_u32(640U));
            add_target_profile_field(store, setup, "exififd", 0xa003U,
                                     make_u32(480U));
            add_target_profile_field(store, setup, "exififd", 0x9101U,
                                     make_bytes(store->arena(), components));
            add_target_profile_field(store, setup, "ifd0", 0x0213U,
                                     make_u16(1U));
            return;
        }

        const bool ycbcr = profile == MetadataTargetProfile::ExifTiffYCbCr;
        add_target_profile_field(store, setup, "ifd0", 0x0100U, make_u32(640U));
        add_target_profile_field(store, setup, "ifd0", 0x0101U, make_u32(480U));
        add_target_profile_field(store, setup, "ifd0", 0x0102U,
                                 target_test_u16_array(store, bits));
        add_target_profile_field(store, setup, "ifd0", 0x0103U, make_u16(1U));
        add_target_profile_field(store, setup, "ifd0", 0x0106U,
                                 make_u16(ycbcr ? 6U : 2U));
        add_target_profile_field(store, setup, "ifd0", 0x0115U, make_u16(3U));
        if (profile == MetadataTargetProfile::ExifTiffRgbPlanar) {
            add_target_profile_field(store, setup, "ifd0", 0x011cU,
                                     make_u16(2U));
        }
        if (ycbcr) {
            add_target_profile_field(store, setup, "ifd0", 0x0212U,
                                     target_test_u16_array(store, subsampling));
            add_target_profile_field(store, setup, "ifd0", 0x0213U,
                                     make_u16(1U));
        }
    }

    static MetaStore make_target_profile_store(MetadataTargetProfile profile,
                                               const TargetTestSetup& setup = {})
    {
        MetaStore store;
        append_target_profile_entries(&store, profile, setup);
        store.finalize();
        return store;
    }

    static MetadataValidationOptions
    target_validation_options(MetadataTargetProfile profile)
    {
        MetadataValidationOptions options;
        options.context.has_dimensions        = true;
        options.context.width                 = 640U;
        options.context.height                = 480U;
        options.context.has_samples_per_pixel = true;
        options.context.samples_per_pixel     = 3U;
        options.target_profile.profile        = profile;
        options.target_profile.exif_version   = 300U;
        return options;
    }

    enum class BadTargetValue : uint8_t {
        ExifVersionMismatch,
        ColorSpaceReserved,
        DimensionMismatch,
        ComponentsOrder,
        BitsPerSampleValue,
        BitsPerSampleCount,
        CompressionValue,
        CompressionType,
        SamplesPerPixelValue,
        PhotometricValue,
        PlanarConfigurationValue,
        PlanarConfigurationChunky,
        YCbCrSubSamplingValue,
        YCbCrPositioningValue,
    };

    static MetaValue make_bad_target_value(MetaStore* store, BadTargetValue bad)
    {
        static constexpr std::array<std::byte, 4> version_0310 {
            std::byte { '0' }, std::byte { '3' }, std::byte { '1' },
            std::byte { '0' }
        };
        static constexpr std::array<std::byte, 4> components_wrong {
            std::byte { 1U }, std::byte { 3U }, std::byte { 2U },
            std::byte { 0U }
        };
        static constexpr std::array<uint16_t, 3> bits_wrong { 8U, 8U, 16U };
        static constexpr std::array<uint16_t, 2> bits_short { 8U, 8U };
        static constexpr std::array<uint16_t, 2> subsampling_wrong { 2U, 4U };
        switch (bad) {
        case BadTargetValue::ExifVersionMismatch:
            return make_bytes(store->arena(), version_0310);
        case BadTargetValue::ColorSpaceReserved: return make_u16(2U);
        case BadTargetValue::DimensionMismatch: return make_u32(641U);
        case BadTargetValue::ComponentsOrder:
            return make_bytes(store->arena(), components_wrong);
        case BadTargetValue::BitsPerSampleValue:
            return target_test_u16_array(store, bits_wrong);
        case BadTargetValue::BitsPerSampleCount:
            return target_test_u16_array(store, bits_short);
        case BadTargetValue::CompressionValue: return make_u16(2U);
        case BadTargetValue::CompressionType: return make_u32(1U);
        case BadTargetValue::SamplesPerPixelValue: return make_u16(4U);
        case BadTargetValue::PhotometricValue: return make_u16(2U);
        case BadTargetValue::PlanarConfigurationValue: return make_u16(3U);
        case BadTargetValue::PlanarConfigurationChunky: return make_u16(2U);
        case BadTargetValue::YCbCrSubSamplingValue:
            return target_test_u16_array(store, subsampling_wrong);
        case BadTargetValue::YCbCrPositioningValue: return make_u16(3U);
        }
        return MetaValue {};
    }

    static bool has_target_issue(const MetadataValidationResult& result,
                                 MetadataValidationIssueCode code,
                                 uint16_t tag) noexcept
    {
        for (const MetadataValidationIssue& issue : result.issues) {
            if (issue.code == code && issue.tag == tag) {
                return true;
            }
        }
        return false;
    }

    static void
    expect_target_validation_ok(const MetadataValidationResult& result)
    {
        EXPECT_EQ(result.status, MetadataValidationStatus::Ok)
            << metadata_validation_status_name(result.status);
        for (const MetadataValidationIssue& issue : result.issues) {
            EXPECT_NE(issue.severity, ValidateIssueSeverity::Error)
                << metadata_validation_issue_code_name(issue.code) << ':'
                << issue.tag;
        }
    }

    TEST(MetadataStandardValidation,
         JpegTargetProfileRequiresPrimaryComponentsEvenWithoutSchema)
    {
        constexpr std::array<std::byte, 4> exif_version { std::byte { '0' },
                                                          std::byte { '3' },
                                                          std::byte { '0' },
                                                          std::byte { '0' } };
        MetaStore store;
        const EntryId version_id
            = add_target_test_entry(&store, "exififd", 0x9000U,
                                    make_bytes(store.arena(), exif_version));
        add_target_test_entry(&store, "exififd", 0xa001U, make_u16(1U));
        add_target_test_entry(&store, "exififd", 0xa002U, make_u32(640U));
        add_target_test_entry(&store, "exififd", 0xa003U, make_u32(480U));
        add_target_test_entry(&store, "ifd0", 0x0213U, make_u16(1U));
        store.finalize();

        EXPECT_TRUE(validate_store(store).ok());

        MetadataValidationOptions options;
        options.validate_schema               = false;
        options.context.has_dimensions        = true;
        options.context.width                 = 640U;
        options.context.height                = 480U;
        options.context.has_samples_per_pixel = true;
        options.context.samples_per_pixel     = 3U;
        options.target_profile.profile        = MetadataTargetProfile::ExifJpeg;
        options.target_profile.exif_version   = 300U;

        const MetadataValidationResult entry_result
            = validate_entry(store, version_id, options);
        EXPECT_TRUE(entry_result.ok());

        const MetadataValidationResult store_result = validate_store(store,
                                                                     options);
        EXPECT_EQ(store_result.status,
                  MetadataValidationStatus::InvalidMetadata);
        bool found_missing_components = false;
        for (const MetadataValidationIssue& issue : store_result.issues) {
            if (issue.code
                    == MetadataValidationIssueCode::TargetProfileMissingRequiredField
                && issue.tag == 0x9101U) {
                found_missing_components = true;
            }
        }
        EXPECT_TRUE(found_missing_components);
    }

    TEST(MetadataStandardValidation, AllTargetProfileLayoutsValidate)
    {
        constexpr std::array<MetadataTargetProfile, 4> profiles {
            MetadataTargetProfile::ExifJpeg,
            MetadataTargetProfile::ExifTiffRgbChunky,
            MetadataTargetProfile::ExifTiffRgbPlanar,
            MetadataTargetProfile::ExifTiffYCbCr,
        };
        for (MetadataTargetProfile profile : profiles) {
            MetaStore store = make_target_profile_store(profile);
            const MetadataValidationResult result
                = validate_store(store, target_validation_options(profile));
            SCOPED_TRACE(static_cast<unsigned>(profile));
            expect_target_validation_ok(result);
        }
    }

    TEST(MetadataStandardValidation, EachTargetProfileRequiresItsPrimaryFields)
    {
        struct RequiredField final {
            MetadataTargetProfile profile;
            TargetTestField field;
        };
        static constexpr RequiredField required[] = {
            { MetadataTargetProfile::ExifJpeg, { "exififd", 0x9000U } },
            { MetadataTargetProfile::ExifJpeg, { "exififd", 0xa001U } },
            { MetadataTargetProfile::ExifJpeg, { "exififd", 0xa002U } },
            { MetadataTargetProfile::ExifJpeg, { "exififd", 0xa003U } },
            { MetadataTargetProfile::ExifJpeg, { "exififd", 0x9101U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0213U } },

            { MetadataTargetProfile::ExifTiffRgbChunky, { "exififd", 0x9000U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "exififd", 0xa001U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "ifd0", 0x0100U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "ifd0", 0x0101U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "ifd0", 0x0102U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "ifd0", 0x0103U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "ifd0", 0x0106U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "ifd0", 0x0115U } },

            { MetadataTargetProfile::ExifTiffRgbPlanar, { "exififd", 0x9000U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "exififd", 0xa001U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x0100U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x0101U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x0102U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x0103U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x0106U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x0115U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x011cU } },

            { MetadataTargetProfile::ExifTiffYCbCr, { "exififd", 0x9000U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "exififd", 0xa001U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "ifd0", 0x0100U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "ifd0", 0x0101U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "ifd0", 0x0102U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "ifd0", 0x0103U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "ifd0", 0x0106U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "ifd0", 0x0115U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "ifd0", 0x0212U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "ifd0", 0x0213U } },
        };
        for (const RequiredField& item : required) {
            TargetTestSetup setup;
            setup.omitted   = item.field;
            MetaStore store = make_target_profile_store(item.profile, setup);
            MetadataValidationOptions options = target_validation_options(
                item.profile);
            options.validate_schema               = false;
            const MetadataValidationResult result = validate_store(store,
                                                                   options);
            EXPECT_EQ(result.status, MetadataValidationStatus::InvalidMetadata)
                << item.field.ifd << ':' << item.field.tag;
            EXPECT_TRUE(has_target_issue(
                result,
                MetadataValidationIssueCode::TargetProfileMissingRequiredField,
                item.field.tag))
                << item.field.ifd << ':' << item.field.tag;
        }
    }

    TEST(MetadataStandardValidation, TargetProfilesRejectForbiddenFields)
    {
        struct ForbiddenField final {
            MetadataTargetProfile profile;
            TargetTestField field;
        };
        static constexpr ForbiddenField forbidden[] = {
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0100U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0101U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0102U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0103U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0106U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0111U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0115U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0116U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0117U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x011cU } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0201U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0202U } },
            { MetadataTargetProfile::ExifJpeg, { "ifd0", 0x0212U } },

            { MetadataTargetProfile::ExifTiffRgbChunky, { "ifd0", 0x0201U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "ifd0", 0x0202U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "ifd0", 0x0211U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "ifd0", 0x0212U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "ifd0", 0x0213U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "exififd", 0x9101U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "exififd", 0x9102U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "exififd", 0xa002U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "exififd", 0xa003U } },
            { MetadataTargetProfile::ExifTiffRgbChunky, { "exififd", 0xa005U } },
            { MetadataTargetProfile::ExifTiffRgbChunky,
              { "interopifd", 0x1000U } },

            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x0201U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x0202U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x0211U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x0212U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "ifd0", 0x0213U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "exififd", 0x9101U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "exififd", 0x9102U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "exififd", 0xa002U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "exififd", 0xa003U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar, { "exififd", 0xa005U } },
            { MetadataTargetProfile::ExifTiffRgbPlanar,
              { "interopifd", 0x1000U } },

            { MetadataTargetProfile::ExifTiffYCbCr, { "ifd0", 0x0201U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "ifd0", 0x0202U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "exififd", 0x9101U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "exififd", 0x9102U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "exififd", 0xa002U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "exififd", 0xa003U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "exififd", 0xa005U } },
            { MetadataTargetProfile::ExifTiffYCbCr, { "interopifd", 0x1000U } },
        };
        for (const ForbiddenField& item : forbidden) {
            MetaStore store;
            append_target_profile_entries(&store, item.profile);
            add_target_test_entry(&store, item.field.ifd, item.field.tag,
                                  make_u16(1U));
            store.finalize();
            MetadataValidationOptions options = target_validation_options(
                item.profile);
            options.validate_schema               = false;
            const MetadataValidationResult result = validate_store(store,
                                                                   options);
            EXPECT_TRUE(has_target_issue(
                result,
                MetadataValidationIssueCode::TargetProfileForbiddenField,
                item.field.tag))
                << static_cast<unsigned>(item.profile) << ':' << item.field.ifd
                << ':' << item.field.tag;
        }
    }

    TEST(MetadataStandardValidation,
         TargetProfilesRejectIndependentValueAndLayoutConflicts)
    {
        struct InvalidField final {
            MetadataTargetProfile profile;
            TargetTestField field;
            BadTargetValue value;
        };
        static constexpr InvalidField invalid[] = {
            { MetadataTargetProfile::ExifJpeg,
              { "exififd", 0x9000U },
              BadTargetValue::ExifVersionMismatch },
            { MetadataTargetProfile::ExifJpeg,
              { "exififd", 0xa001U },
              BadTargetValue::ColorSpaceReserved },
            { MetadataTargetProfile::ExifJpeg,
              { "exififd", 0xa002U },
              BadTargetValue::DimensionMismatch },
            { MetadataTargetProfile::ExifJpeg,
              { "exififd", 0x9101U },
              BadTargetValue::ComponentsOrder },
            { MetadataTargetProfile::ExifJpeg,
              { "ifd0", 0x0213U },
              BadTargetValue::YCbCrPositioningValue },

            { MetadataTargetProfile::ExifTiffRgbChunky,
              { "ifd0", 0x0100U },
              BadTargetValue::DimensionMismatch },
            { MetadataTargetProfile::ExifTiffRgbChunky,
              { "ifd0", 0x0102U },
              BadTargetValue::BitsPerSampleValue },
            { MetadataTargetProfile::ExifTiffRgbChunky,
              { "ifd0", 0x0102U },
              BadTargetValue::BitsPerSampleCount },
            { MetadataTargetProfile::ExifTiffRgbChunky,
              { "ifd0", 0x0103U },
              BadTargetValue::CompressionValue },
            { MetadataTargetProfile::ExifTiffRgbChunky,
              { "ifd0", 0x0103U },
              BadTargetValue::CompressionType },
            { MetadataTargetProfile::ExifTiffRgbChunky,
              { "ifd0", 0x0115U },
              BadTargetValue::SamplesPerPixelValue },
            { MetadataTargetProfile::ExifTiffRgbChunky,
              { "ifd0", 0x011cU },
              BadTargetValue::PlanarConfigurationChunky },

            { MetadataTargetProfile::ExifTiffRgbPlanar,
              { "ifd0", 0x011cU },
              BadTargetValue::PlanarConfigurationValue },

            { MetadataTargetProfile::ExifTiffYCbCr,
              { "ifd0", 0x0106U },
              BadTargetValue::PhotometricValue },
            { MetadataTargetProfile::ExifTiffYCbCr,
              { "ifd0", 0x0212U },
              BadTargetValue::YCbCrSubSamplingValue },
            { MetadataTargetProfile::ExifTiffYCbCr,
              { "ifd0", 0x0213U },
              BadTargetValue::YCbCrPositioningValue },
            { MetadataTargetProfile::ExifTiffYCbCr,
              { "ifd0", 0x011cU },
              BadTargetValue::PlanarConfigurationValue },
        };
        for (const InvalidField& item : invalid) {
            TargetTestSetup setup;
            setup.omitted = item.field;
            MetaStore store;
            append_target_profile_entries(&store, item.profile, setup);
            add_target_test_entry(&store, item.field.ifd, item.field.tag,
                                  make_bad_target_value(&store, item.value));
            store.finalize();
            MetadataValidationOptions options = target_validation_options(
                item.profile);
            options.validate_schema               = false;
            const MetadataValidationResult result = validate_store(store,
                                                                   options);
            EXPECT_TRUE(has_target_issue(
                result,
                MetadataValidationIssueCode::TargetProfileContextConflict,
                item.field.tag))
                << static_cast<unsigned>(item.profile) << ':' << item.field.ifd
                << ':' << item.field.tag;
        }
    }

    TEST(MetadataStandardValidation,
         TargetProfileEnforcesWireHintsWhenGenericChecksAreDisabled)
    {
        struct WireMismatch final {
            TargetTestField field;
            WireType wire_type;
            uint32_t wire_count;
            bool validate_schema;
            bool validate_wire_hints;
        };
        static constexpr WireMismatch invalid[] = {
            { { "exififd", 0x9000U },
              { WireFamily::Tiff, 1U },
              4U,
              false,
              false },
            { { "ifd0", 0x0213U }, { WireFamily::Tiff, 4U }, 1U, false, false },
            { { "exififd", 0xa001U },
              { WireFamily::Tiff, 3U },
              2U,
              false,
              false },
            { { "exififd", 0x9000U }, { WireFamily::Tiff, 1U }, 4U, false, true },
            { { "exififd", 0x9000U }, { WireFamily::Tiff, 1U }, 4U, true, false },
        };
        for (const WireMismatch& item : invalid) {
            TargetTestSetup setup;
            setup.wire_field = item.field;
            setup.wire_type  = item.wire_type;
            setup.wire_count = item.wire_count;
            MetaStore store
                = make_target_profile_store(MetadataTargetProfile::ExifJpeg,
                                            setup);
            MetadataValidationOptions options = target_validation_options(
                MetadataTargetProfile::ExifJpeg);
            options.validate_schema               = item.validate_schema;
            options.validate_wire_hints           = item.validate_wire_hints;
            const MetadataValidationResult result = validate_store(store,
                                                                   options);
            EXPECT_TRUE(has_target_issue(
                result,
                MetadataValidationIssueCode::TargetProfileContextConflict,
                item.field.tag))
                << item.field.ifd << ':' << item.field.tag << ':'
                << item.validate_schema << ':' << item.validate_wire_hints;
        }

        MetaStore no_hints = make_target_profile_store(
            MetadataTargetProfile::ExifJpeg);
        MetadataValidationOptions options = target_validation_options(
            MetadataTargetProfile::ExifJpeg);
        options.validate_schema     = false;
        options.validate_wire_hints = false;
        EXPECT_TRUE(validate_store(no_hints, options).ok());

        TargetTestSetup inferred_count;
        inferred_count.wire_field = { "exififd", 0x9000U };
        inferred_count.wire_type  = { WireFamily::Tiff, 7U };
        MetaStore zero_wire_count
            = make_target_profile_store(MetadataTargetProfile::ExifJpeg,
                                        inferred_count);
        EXPECT_TRUE(validate_store(zero_wire_count, options).ok());
    }

    TEST(MetadataStandardValidation,
         TargetProfileInteropNeedsHostAuthorityAndExactIndex)
    {
        MetaStore native;
        append_target_profile_entries(&native, MetadataTargetProfile::ExifJpeg);
        add_target_test_entry(&native, "interopifd", 0x0001U,
                              make_text(native.arena(), "R98",
                                        TextEncoding::Ascii),
                              EntryFlags::None,
                              WireType { WireFamily::Tiff, 2U }, 4U);
        native.finalize();
        MetadataValidationOptions options = target_validation_options(
            MetadataTargetProfile::ExifJpeg);
        options.target_profile.interoperability
            = MetadataTargetInteropIndex::R98;
        EXPECT_TRUE(validate_store(native, options).ok());

        options.target_profile.has_color_space = true;
        options.target_profile.color_space     = 1U;
        EXPECT_TRUE(validate_store(native, options).ok());
        options.target_profile.color_space = 0xffffU;
        EXPECT_TRUE(has_target_issue(
            validate_store(native, options),
            MetadataValidationIssueCode::TargetProfileContextConflict,
            0xa001U));
        options.target_profile.has_color_space = false;

        struct InteropCase final {
            MetadataTargetInteropIndex index;
            std::string_view text;
        };
        static constexpr InteropCase interoperabilities[] = {
            { MetadataTargetInteropIndex::R03, "R03" },
            { MetadataTargetInteropIndex::THM, "THM" },
        };
        for (const InteropCase& item : interoperabilities) {
            MetaStore matching;
            append_target_profile_entries(&matching,
                                          MetadataTargetProfile::ExifJpeg);
            add_target_test_entry(&matching, "interopifd", 0x0001U,
                                  make_text(matching.arena(), item.text,
                                            TextEncoding::Ascii),
                                  EntryFlags::None,
                                  WireType { WireFamily::Tiff, 2U }, 4U);
            matching.finalize();
            options.target_profile.interoperability = item.index;
            EXPECT_TRUE(validate_store(matching, options).ok()) << item.text;
        }
        options.target_profile.interoperability
            = MetadataTargetInteropIndex::R98;

        MetaStore untrusted;
        append_target_profile_entries(&untrusted,
                                      MetadataTargetProfile::ExifJpeg);
        add_target_test_entry(&untrusted, "interopifd", 0x1000U,
                              make_text(untrusted.arena(), "JPEG",
                                        TextEncoding::Ascii));
        untrusted.finalize();
        MetadataValidationOptions no_authority_options = options;
        no_authority_options.target_profile.interoperability
            = MetadataTargetInteropIndex::Unspecified;
        const MetadataValidationResult untrusted_result
            = validate_store(untrusted, no_authority_options);
        EXPECT_TRUE(has_target_issue(
            untrusted_result,
            MetadataValidationIssueCode::TargetProfileMissingAuthority,
            0x1000U));

        EXPECT_TRUE(validate_store(native, options).ok());

        MetaStore absent;
        append_target_profile_entries(&absent, MetadataTargetProfile::ExifJpeg);
        absent.finalize();
        const MetadataValidationResult absent_result = validate_store(absent,
                                                                      options);
        EXPECT_TRUE(has_target_issue(
            absent_result,
            MetadataValidationIssueCode::TargetProfileMissingRequiredField,
            0x0001U));

        MetaStore mismatch;
        append_target_profile_entries(&mismatch,
                                      MetadataTargetProfile::ExifJpeg);
        add_target_test_entry(&mismatch, "interopifd", 0x0001U,
                              make_text(mismatch.arena(), "R03",
                                        TextEncoding::Ascii),
                              EntryFlags::None,
                              WireType { WireFamily::Tiff, 2U }, 4U);
        mismatch.finalize();
        const MetadataValidationResult mismatch_result
            = validate_store(mismatch, options);
        EXPECT_TRUE(has_target_issue(
            mismatch_result,
            MetadataValidationIssueCode::TargetProfileContextConflict,
            0x0001U));

        MetaStore pointer_only;
        append_target_profile_entries(&pointer_only,
                                      MetadataTargetProfile::ExifJpeg);
        add_target_test_entry(&pointer_only, "exififd", 0xa005U, make_u32(1U));
        pointer_only.finalize();
        options.target_profile.interoperability
            = MetadataTargetInteropIndex::Unspecified;
        const MetadataValidationResult pointer_result
            = validate_store(pointer_only, options);
        EXPECT_TRUE(has_target_issue(
            pointer_result,
            MetadataValidationIssueCode::TargetProfileMissingAuthority,
            0xa005U));
    }

    TEST(MetadataStandardValidation,
         TargetProfileRejectsInvalidContextAndPreservesDetachedDefault)
    {
        MetaStore empty;
        empty.finalize();
        std::array<MetadataValidationOptions, 8> invalid {};
        for (MetadataValidationOptions& options : invalid) {
            options = target_validation_options(
                MetadataTargetProfile::ExifTiffRgbChunky);
        }
        invalid[0].target_profile.exif_version    = 0U;
        invalid[1].context.has_dimensions         = false;
        invalid[2].context.has_samples_per_pixel  = false;
        invalid[3].context.samples_per_pixel      = 2U;
        invalid[4].target_profile.has_color_space = true;
        invalid[4].target_profile.color_space     = 2U;
        invalid[5].target_profile.interoperability
            = MetadataTargetInteropIndex::R98;
        invalid[6].target_profile.profile = MetadataTargetProfile::ExifJpeg;
        invalid[6].target_profile.require_storage_tags = true;
        invalid[7].target_profile.profile = static_cast<MetadataTargetProfile>(
            255U);
        for (const MetadataValidationOptions& options : invalid) {
            EXPECT_EQ(validate_store(empty, options).status,
                      MetadataValidationStatus::InvalidArgument);
        }

        EXPECT_TRUE(validate_store(empty).ok());
    }

    TEST(MetadataStandardValidation,
         TargetProfileStorageRequiresPairedStripsAndExactPlaneCounts)
    {
        static constexpr std::array<uint32_t, 2> chunky_offsets { 16U, 32U };
        static constexpr std::array<uint32_t, 2> chunky_counts { 100U, 200U };
        static constexpr std::array<uint32_t, 6> planar_offsets {
            16U, 32U, 48U, 64U, 80U, 96U
        };
        static constexpr std::array<uint32_t, 6> planar_counts { 100U, 110U,
                                                                 120U, 130U,
                                                                 140U, 150U };

        MetaStore chunky;
        append_target_profile_entries(&chunky,
                                      MetadataTargetProfile::ExifTiffRgbChunky);
        add_target_test_entry(&chunky, "ifd0", 0x0116U, make_u32(240U));
        add_target_test_entry(&chunky, "ifd0", 0x0111U,
                              target_test_u32_array(&chunky, chunky_offsets));
        add_target_test_entry(&chunky, "ifd0", 0x0117U,
                              target_test_u32_array(&chunky, chunky_counts));
        chunky.finalize();
        MetadataValidationOptions options = target_validation_options(
            MetadataTargetProfile::ExifTiffRgbChunky);
        options.target_profile.require_storage_tags = true;
        expect_target_validation_ok(validate_store(chunky, options));

        MetaStore absent;
        append_target_profile_entries(&absent,
                                      MetadataTargetProfile::ExifTiffRgbChunky);
        absent.finalize();
        const MetadataValidationResult absent_result = validate_store(absent,
                                                                      options);
        EXPECT_TRUE(has_target_issue(
            absent_result,
            MetadataValidationIssueCode::TargetProfileMissingRequiredField,
            0x0116U));
        EXPECT_TRUE(has_target_issue(
            absent_result,
            MetadataValidationIssueCode::TargetProfileMissingRequiredField,
            0x0111U));
        EXPECT_TRUE(has_target_issue(
            absent_result,
            MetadataValidationIssueCode::TargetProfileMissingRequiredField,
            0x0117U));

        MetaStore pair_mismatch;
        append_target_profile_entries(&pair_mismatch,
                                      MetadataTargetProfile::ExifTiffRgbChunky);
        add_target_test_entry(&pair_mismatch, "ifd0", 0x0116U, make_u32(240U));
        add_target_test_entry(
            &pair_mismatch, "ifd0", 0x0111U,
            target_test_u32_array(&pair_mismatch,
                                  std::span(chunky_offsets).first(1U)));
        add_target_test_entry(&pair_mismatch, "ifd0", 0x0117U,
                              target_test_u32_array(&pair_mismatch,
                                                    chunky_counts));
        pair_mismatch.finalize();
        EXPECT_TRUE(has_target_issue(
            validate_store(pair_mismatch, options),
            MetadataValidationIssueCode::TargetProfileContextConflict,
            0x0111U));

        MetaStore planar;
        append_target_profile_entries(&planar,
                                      MetadataTargetProfile::ExifTiffRgbPlanar);
        add_target_test_entry(&planar, "ifd0", 0x0116U, make_u32(240U));
        add_target_test_entry(&planar, "ifd0", 0x0111U,
                              target_test_u32_array(&planar, planar_offsets));
        add_target_test_entry(&planar, "ifd0", 0x0117U,
                              target_test_u32_array(&planar, planar_counts));
        planar.finalize();
        options = target_validation_options(
            MetadataTargetProfile::ExifTiffRgbPlanar);
        options.target_profile.require_storage_tags = true;
        expect_target_validation_ok(validate_store(planar, options));

        MetaStore planar_short;
        append_target_profile_entries(&planar_short,
                                      MetadataTargetProfile::ExifTiffRgbPlanar);
        add_target_test_entry(&planar_short, "ifd0", 0x0116U, make_u32(240U));
        add_target_test_entry(&planar_short, "ifd0", 0x0111U,
                              target_test_u32_array(&planar_short,
                                                    chunky_offsets));
        add_target_test_entry(&planar_short, "ifd0", 0x0117U,
                              target_test_u32_array(&planar_short,
                                                    chunky_counts));
        planar_short.finalize();
        EXPECT_TRUE(has_target_issue(
            validate_store(planar_short, options),
            MetadataValidationIssueCode::TargetProfileContextConflict,
            0x0111U));

        MetaStore ycbcr_chunky;
        append_target_profile_entries(&ycbcr_chunky,
                                      MetadataTargetProfile::ExifTiffYCbCr);
        add_target_test_entry(&ycbcr_chunky, "ifd0", 0x0116U, make_u32(240U));
        add_target_test_entry(&ycbcr_chunky, "ifd0", 0x0111U,
                              target_test_u32_array(&ycbcr_chunky,
                                                    chunky_offsets));
        add_target_test_entry(&ycbcr_chunky, "ifd0", 0x0117U,
                              target_test_u32_array(&ycbcr_chunky,
                                                    chunky_counts));
        ycbcr_chunky.finalize();
        options = target_validation_options(
            MetadataTargetProfile::ExifTiffYCbCr);
        options.target_profile.require_storage_tags = true;
        expect_target_validation_ok(validate_store(ycbcr_chunky, options));

        MetaStore ycbcr_planar;
        append_target_profile_entries(&ycbcr_planar,
                                      MetadataTargetProfile::ExifTiffYCbCr);
        add_target_test_entry(&ycbcr_planar, "ifd0", 0x011cU, make_u16(2U));
        add_target_test_entry(&ycbcr_planar, "ifd0", 0x0116U, make_u32(240U));
        add_target_test_entry(&ycbcr_planar, "ifd0", 0x0111U,
                              target_test_u32_array(&ycbcr_planar,
                                                    planar_offsets));
        add_target_test_entry(&ycbcr_planar, "ifd0", 0x0117U,
                              target_test_u32_array(&ycbcr_planar,
                                                    planar_counts));
        ycbcr_planar.finalize();
        expect_target_validation_ok(validate_store(ycbcr_planar, options));
    }

    TEST(MetadataStandardValidation,
         TargetProfileUsesOnlyActiveExactPrimaryEntries)
    {
        TargetTestSetup omitted_width;
        omitted_width.omitted = { "ifd0", 0x0100U };
        MetaStore secondary   = make_target_profile_store(
            MetadataTargetProfile::ExifTiffRgbChunky, omitted_width);
        add_target_test_entry(&secondary, "ifd1", 0x0100U, make_u32(640U));
        secondary.finalize();
        MetadataValidationOptions options = target_validation_options(
            MetadataTargetProfile::ExifTiffRgbChunky);
        options.validate_schema = false;
        EXPECT_TRUE(has_target_issue(
            validate_store(secondary, options),
            MetadataValidationIssueCode::TargetProfileMissingRequiredField,
            0x0100U));

        TargetTestSetup deleted_version;
        deleted_version.flagged = { "exififd", 0x9000U };
        deleted_version.flags   = EntryFlags::Deleted;
        MetaStore tombstone
            = make_target_profile_store(MetadataTargetProfile::ExifJpeg,
                                        deleted_version);
        EXPECT_TRUE(has_target_issue(
            validate_store(tombstone, target_validation_options(
                                          MetadataTargetProfile::ExifJpeg)),
            MetadataValidationIssueCode::TargetProfileMissingRequiredField,
            0x9000U));

        static constexpr std::array<std::byte, 4> version { std::byte { '0' },
                                                            std::byte { '3' },
                                                            std::byte { '0' },
                                                            std::byte { '0' } };
        MetaStore one_active_version;
        append_target_profile_entries(&one_active_version,
                                      MetadataTargetProfile::ExifJpeg,
                                      deleted_version);
        add_target_test_entry(&one_active_version, "exififd", 0x9000U,
                              make_bytes(one_active_version.arena(), version));
        one_active_version.finalize();
        EXPECT_TRUE(validate_store(one_active_version,
                                   target_validation_options(
                                       MetadataTargetProfile::ExifJpeg))
                        .ok());

        MetaStore duplicate_version;
        append_target_profile_entries(&duplicate_version,
                                      MetadataTargetProfile::ExifJpeg);
        add_target_test_entry(&duplicate_version, "exififd", 0x9000U,
                              make_bytes(duplicate_version.arena(), version));
        duplicate_version.finalize();
        options = target_validation_options(MetadataTargetProfile::ExifJpeg);
        options.validate_schema = false;
        EXPECT_TRUE(has_target_issue(
            validate_store(duplicate_version, options),
            MetadataValidationIssueCode::TargetProfileContextConflict,
            0x9000U));

        constexpr std::array<EntryFlags, 2> bad_flags {
            EntryFlags::Truncated, EntryFlags::Unreadable
        };
        for (EntryFlags flags : bad_flags) {
            TargetTestSetup malformed_components;
            malformed_components.flagged = { "exififd", 0x9101U };
            malformed_components.flags   = flags;
            MetaStore malformed
                = make_target_profile_store(MetadataTargetProfile::ExifJpeg,
                                            malformed_components);
            options = target_validation_options(
                MetadataTargetProfile::ExifJpeg);
            options.validate_schema = false;
            EXPECT_TRUE(has_target_issue(
                validate_store(malformed, options),
                MetadataValidationIssueCode::TargetProfileContextConflict,
                0x9101U))
                << static_cast<unsigned>(flags);
        }
    }

    TEST(MetadataStandardValidation,
         TargetProfileVersionPolicyAndIssueLimitStayBounded)
    {
        static constexpr std::array<std::byte, 4> version_0310 {
            std::byte { '0' }, std::byte { '3' }, std::byte { '1' },
            std::byte { '0' }
        };
        TargetTestSetup omit_version;
        omit_version.omitted = { "exififd", 0x9000U };
        MetaStore version_310;
        append_target_profile_entries(&version_310,
                                      MetadataTargetProfile::ExifJpeg,
                                      omit_version);
        add_target_test_entry(&version_310, "exififd", 0x9000U,
                              make_bytes(version_310.arena(), version_0310));
        version_310.finalize();
        MetadataValidationOptions options = target_validation_options(
            MetadataTargetProfile::ExifJpeg);
        options.target_profile.exif_version = 310U;
        expect_target_validation_ok(validate_store(version_310, options));

        MetaStore empty;
        empty.finalize();
        options = target_validation_options(MetadataTargetProfile::ExifJpeg);
        options.max_issues                    = 1U;
        const MetadataValidationResult capped = validate_store(empty, options);
        EXPECT_EQ(capped.status, MetadataValidationStatus::LimitExceeded);
        EXPECT_LE(capped.issues.size(), 1U);
    }

    TEST(MetadataStandardValidation,
         TerminatedImageIdRetainsWireCountDuringTypedEdits)
    {
        constexpr std::string_view identity = "00112233445566778899aAbBcCdDeEfF";
        std::string terminated(identity);
        terminated.push_back('\0');
        MetaStore source;
        Entry native;
        native.key = make_exif_tag_key(source.arena(), "exififd", 0xA420U);
        native.value = make_text(source.arena(), terminated, TextEncoding::Ascii);
        native.origin.wire_type = { WireFamily::Tiff, 2U };
        native.origin.wire_count = 33U;
        source.add_entry(native);
        source.finalize();
        ASSERT_TRUE(validate_store(source).ok());

        MetadataTypedEditingOperation operation;
        operation.entry.key = make_xmp_property_key_view(
            "http://ns.adobe.com/exif/1.0/", "ExposureIndex");
        operation.entry.value = make_value_view_text("200", TextEncoding::Utf8);
        MetaStore edited;
        ASSERT_TRUE(edit_metadata_typed(source, std::span(&operation, 1U),
                                         &edited).ok());
        ASSERT_TRUE(validate_store(edited).ok());
        const Entry& retained = edited.entry(0U);
        EXPECT_EQ(retained.origin.wire_count, 33U);
        EXPECT_EQ(retained.value.count, 33U);
        const auto retained_bytes = edited.arena().span(retained.value.data.span);
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(retained_bytes.data()),
                                   retained_bytes.size()), terminated);

        MetaStore decoded;
        const auto serialized = canonical(edited);
        ASSERT_EQ(decode_exif_tiff(serialized, decoded, {}, {}).status,
                  ExifDecodeStatus::Ok);
        decoded.finalize();
        EXPECT_TRUE(validate_store(decoded).ok());
        const auto identities = decoded.find_all(
            make_exif_tag_key_view("exififd", 0xA420U));
        ASSERT_EQ(identities.size(), 1U);
        EXPECT_EQ(decoded.entry(identities[0U]).origin.wire_count, 33U);

        for (unsigned variant = 0U; variant < 3U; ++variant) {
            MetaStore invalid;
            std::string text = terminated;
            if (variant == 0U) text.push_back('\0');
            if (variant == 1U) text[12U] = '\0';
            Entry entry;
            entry.key = make_exif_tag_key(invalid.arena(), "exififd", 0xA420U);
            entry.value = make_text(invalid.arena(), text, TextEncoding::Ascii);
            entry.origin.wire_type = { WireFamily::Tiff, 2U };
            entry.origin.wire_count = variant == 2U ? 34U : 33U;
            invalid.add_entry(entry);
            invalid.finalize();
            EXPECT_FALSE(validate_store(invalid).ok()) << variant;
            const auto rejected = edit_metadata_typed(
                invalid, std::span(&operation, 1U), &edited);
            EXPECT_EQ(rejected.status, MetadataTypedEditingStatus::ValidationFailed)
                << variant;
            EXPECT_EQ(canonical(edited), serialized) << variant;
        }
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
