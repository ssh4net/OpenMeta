// SPDX-License-Identifier: Apache-2.0

#include <openmeta/build_info.h>
#include <openmeta/exif_tiff_serialize.h>
#include <openmeta/host_adoption.h>
#include <openmeta/metadata_authoring.h>
#include <openmeta/metadata_editing.h>
#include <openmeta/metadata_patch.h>
#include <openmeta/metadata_translation.h>
#include <openmeta/prepared_transfer_handoff.h>
#include <openmeta/xmp_dump.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

int
main()
{
    std::string line1;
    std::string line2;
    openmeta::format_build_info_lines(&line1, &line2);
    const bool profile_matches = openmeta::host_adoption_profile_matches(
        openmeta::kHostAdoptionProfileV1);
    const bool handoff_contract_matches
        = openmeta::prepared_transfer_handoff_contract_version()
          == openmeta::kPreparedTransferHandoffContractVersion;
    const bool instance_contract_matches
        = openmeta::prepared_transfer_handoff_instance_contract_version()
          == openmeta::kPreparedTransferHandoffInstanceContractVersion;
    openmeta::MetaStore translation_source;
    translation_source.finalize();
    openmeta::MetaStore translated;
    const openmeta::MetadataDateTranslationResult translation
        = openmeta::translate_xmp_creation_dates(
            translation_source, openmeta::MetadataDateTranslationOptions {},
            &translated);
    const bool translation_contract_matches
        = openmeta::kMetadataDateTranslationContractVersion == 1U
          && translation.status == openmeta::MetadataDateTranslationStatus::Ok
          && translated.is_finalized();
    openmeta::MetaStore descriptive_translated;
    const openmeta::MetadataDescriptiveTranslationResult descriptive_translation
        = openmeta::translate_xmp_descriptive_metadata(
            translation_source,
            openmeta::MetadataDescriptiveTranslationOptions {},
            &descriptive_translated);
    const bool descriptive_translation_contract_matches
        = openmeta::kMetadataDescriptiveTranslationContractVersion == 1U
          && descriptive_translation.status
                 == openmeta::MetadataDescriptiveTranslationStatus::Ok
          && descriptive_translated.is_finalized();
    const openmeta::MetadataAuthoringEntry orientation {
        openmeta::make_exif_tag_key_view("ifd0", 0x0112U),
        openmeta::make_value_view_u16(1U),
        openmeta::WireType { openmeta::WireFamily::Tiff, 3U },
        1U,
    };
    const openmeta::MetadataAuthoringEntry location {
        openmeta::make_xmp_property_key_view(
            "http://ns.adobe.com/photoshop/1.0/", "City"),
        openmeta::make_value_view_text("Kyoto", openmeta::TextEncoding::Utf8),
    };
    openmeta::MetaStore location_source;
    const auto location_authored = openmeta::create_metadata_store(
        std::span<const openmeta::MetadataAuthoringEntry>(&location, 1U),
        &location_source);
    openmeta::MetaStore location_translated;
    const auto location_translation = openmeta::translate_xmp_location_metadata(
        location_source, openmeta::MetadataLocationTranslationOptions {},
        &location_translated);
    const bool location_contract_matches
        = location_authored.ok()
          && openmeta::kMetadataLocationTranslationContractVersion == 1U
          && location_translation.status
                 == openmeta::MetadataDescriptiveTranslationStatus::Ok
          && location_translation.entries_added == 1U
          && location_translated.is_finalized();
    const openmeta::MetadataAuthoringEntry editorial {
        openmeta::make_xmp_property_key_view(
            "http://ns.adobe.com/photoshop/1.0/", "Headline"),
        openmeta::make_value_view_text("Garden opens",
                                       openmeta::TextEncoding::Utf8),
    };
    openmeta::MetaStore editorial_source;
    const auto editorial_authored = openmeta::create_metadata_store(
        std::span<const openmeta::MetadataAuthoringEntry>(&editorial, 1U),
        &editorial_source);
    openmeta::MetaStore editorial_translated;
    const auto editorial_translation
        = openmeta::translate_xmp_editorial_metadata(
            editorial_source, openmeta::MetadataEditorialTranslationOptions {},
            &editorial_translated);
    const bool editorial_contract_matches
        = editorial_authored.ok()
          && openmeta::kMetadataEditorialTranslationContractVersion == 1U
          && editorial_translation.status
                 == openmeta::MetadataDescriptiveTranslationStatus::Ok
          && editorial_translation.entries_added == 1U
          && editorial_translated.is_finalized();
    const openmeta::MetadataAuthoringEntry iptc {
        openmeta::make_xmp_property_key_view(
            "http://ns.adobe.com/photoshop/1.0/", "Urgency"),
        openmeta::make_value_view_u8(5U),
    };
    openmeta::MetaStore iptc_source;
    const auto iptc_authored = openmeta::create_metadata_store(
        std::span<const openmeta::MetadataAuthoringEntry>(&iptc, 1U),
        &iptc_source);
    openmeta::MetaStore iptc_translated;
    const auto iptc_translation = openmeta::translate_xmp_iptc_metadata(
        iptc_source, openmeta::MetadataIptcTranslationOptions {},
        &iptc_translated);
    const bool iptc_contract_matches
        = iptc_authored.ok()
          && openmeta::kMetadataIptcTranslationContractVersion == 1U
          && iptc_translation.status
                 == openmeta::MetadataDescriptiveTranslationStatus::Ok
          && iptc_translation.entries_added == 1U
          && iptc_translated.is_finalized();
    openmeta::MetaStore authored;
    const openmeta::MetadataAuthoringEntry gps {
        openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                             "GPSLatitude"),
        openmeta::make_value_view_text("35,48.125N",
                                       openmeta::TextEncoding::Utf8),
    };
    openmeta::MetaStore gps_source;
    const auto gps_authored = openmeta::create_metadata_store(
        std::span<const openmeta::MetadataAuthoringEntry>(&gps, 1U),
        &gps_source);
    openmeta::MetaStore gps_translated;
    const auto gps_translation = openmeta::translate_xmp_gps_metadata(
        gps_source, openmeta::MetadataGpsTranslationOptions {},
        &gps_translated);
    const bool gps_contract_matches
        = gps_authored.ok()
          && openmeta::kMetadataGpsTranslationContractVersion == 1U
          && gps_translation.status
                 == openmeta::MetadataGpsTranslationStatus::Ok
          && gps_translation.entries_added == 3U
          && gps_translated.is_finalized();
    const openmeta::MetadataAuthoringEntry structured_location {
        openmeta::make_xmp_property_key_view(
            "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
            "LocationShown[1]/City"),
        openmeta::make_value_view_text("Kyoto", openmeta::TextEncoding::Utf8),
    };
    openmeta::MetaStore structured_location_source;
    const auto structured_location_authored = openmeta::create_metadata_store(
        std::span<const openmeta::MetadataAuthoringEntry>(&structured_location,
                                                          1U),
        &structured_location_source);
    openmeta::MetaStore structured_location_translated;
    const auto structured_location_result
        = openmeta::translate_xmp_structured_location_metadata(
            structured_location_source,
            openmeta::MetadataStructuredLocationTranslationOptions {},
            &structured_location_translated);
    const bool structured_location_contract_matches
        = structured_location_authored.ok()
          && openmeta::kMetadataStructuredLocationTranslationContractVersion
                 == 1U
          && structured_location_result.status
                 == openmeta::MetadataDescriptiveTranslationStatus::Ok
          && structured_location_result.entries_added == 2U
          && structured_location_translated.is_finalized();
    const openmeta::MetadataAuthoringEntry navigation_timestamp {
        openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                             "GPSTimeStamp"),
        openmeta::make_value_view_text("2024-03-01T00:30:12.125+01:00",
                                       openmeta::TextEncoding::Utf8),
    };
    openmeta::MetaStore navigation_source;
    const auto navigation_authored = openmeta::create_metadata_store(
        std::span<const openmeta::MetadataAuthoringEntry>(&navigation_timestamp,
                                                          1U),
        &navigation_source);
    openmeta::MetaStore navigation_translated;
    const auto navigation_result
        = openmeta::translate_xmp_gps_navigation_metadata(
            navigation_source,
            openmeta::MetadataGpsNavigationTranslationOptions {},
            &navigation_translated);
    const bool navigation_contract_matches
        = navigation_authored.ok()
          && openmeta::kMetadataGpsNavigationTranslationContractVersion == 1U
          && navigation_result.status
                 == openmeta::MetadataGpsTranslationStatus::Ok
          && navigation_result.entries_added == 3U
          && navigation_translated.is_finalized();
    const openmeta::MetadataAuthoringEntry destination_latitude {
        openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                             "GPSDestLatitude"),
        openmeta::make_value_view_text("35,48.125S",
                                       openmeta::TextEncoding::Utf8),
    };
    openmeta::MetaStore destination_source;
    const auto destination_authored = openmeta::create_metadata_store(
        std::span<const openmeta::MetadataAuthoringEntry>(&destination_latitude,
                                                          1U),
        &destination_source);
    openmeta::MetaStore destination_translated;
    const auto destination_result
        = openmeta::translate_xmp_gps_destination_metadata(
            destination_source,
            openmeta::MetadataGpsDestinationTranslationOptions {},
            &destination_translated);
    const bool destination_contract_matches
        = destination_authored.ok()
          && openmeta::kMetadataGpsDestinationTranslationContractVersion == 1U
          && destination_result.status
                 == openmeta::MetadataGpsTranslationStatus::Ok
          && destination_result.entries_added == 3U
          && destination_translated.is_finalized();
    const openmeta::MetadataAuthoringEntry quality_dop {
        openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                             "GPSDOP"),
        openmeta::make_value_view_text("7/3", openmeta::TextEncoding::Utf8),
    };
    openmeta::MetaStore quality_source;
    const auto quality_authored = openmeta::create_metadata_store(
        std::span<const openmeta::MetadataAuthoringEntry>(&quality_dop, 1U),
        &quality_source);
    openmeta::MetaStore quality_translated;
    const auto quality_result = openmeta::translate_xmp_gps_quality_metadata(
        quality_source, openmeta::MetadataGpsQualityTranslationOptions {},
        &quality_translated);
    const bool quality_contract_matches
        = quality_authored.ok()
          && openmeta::kMetadataGpsQualityTranslationContractVersion == 1U
          && quality_result.status == openmeta::MetadataGpsTranslationStatus::Ok
          && quality_result.entries_added == 2U
          && quality_translated.is_finalized();
    const openmeta::MetadataAuthoringEntry gps_text_method {
        openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                             "GPSProcessingMethod"),
        openmeta::make_value_view_text("GPS", openmeta::TextEncoding::Utf8),
    };
    openmeta::MetaStore gps_text_source;
    const auto gps_text_authored = openmeta::create_metadata_store(
        std::span<const openmeta::MetadataAuthoringEntry>(&gps_text_method, 1U),
        &gps_text_source);
    openmeta::MetaStore gps_text_translated;
    const auto gps_text_result = openmeta::translate_xmp_gps_text_metadata(
        gps_text_source, openmeta::MetadataGpsTextTranslationOptions {},
        &gps_text_translated);
    const bool gps_text_contract_matches
        = gps_text_authored.ok()
          && openmeta::kMetadataGpsTextTranslationContractVersion == 1U
          && gps_text_result.status
                 == openmeta::MetadataGpsTranslationStatus::Ok
          && gps_text_result.entries_added == 2U
          && gps_text_translated.is_finalized();
    const openmeta::MetadataAuthoringEntry setting_entry {
        openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                             "WhiteBalance"),
        openmeta::make_value_view_text("Manual", openmeta::TextEncoding::Utf8),
    };
    openmeta::MetaStore setting_source;
    const auto setting_authored
        = openmeta::create_metadata_store(std::span(&setting_entry, 1U),
                                          &setting_source);
    openmeta::MetaStore setting_output;
    const auto setting_result
        = openmeta::translate_xmp_capture_settings_metadata(setting_source, {},
                                                            &setting_output);
    const bool setting_contract_matches
        = setting_authored.ok()
          && setting_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && setting_result.entries_added == 1U;
    const openmeta::MetadataAuthoringEntry location_entry {
        openmeta::make_xmp_property_key_view(
            "http://ns.adobe.com/photoshop/1.0/", "City"),
        openmeta::make_value_view_text("Kyoto", openmeta::TextEncoding::Utf8),
    };
    openmeta::MetaStore location_creation_source;
    const auto location_creation_authored
        = openmeta::create_metadata_store(std::span(&location_entry, 1U),
                                          &location_creation_source);
    openmeta::MetadataLocationCreationTranslationOptions location_options;
    location_options.location_kind
        = openmeta::MetadataStructuredLocationKind::Shown;
    location_options.location_index = 1U;
    openmeta::MetaStore location_output;
    const auto location_result
        = openmeta::translate_xmp_location_to_structured_metadata(
            location_creation_source, location_options, &location_output);
    const bool location_creation_contract_matches
        = location_creation_authored.ok()
          && location_result.status
                 == openmeta::MetadataDescriptiveTranslationStatus::Ok
          && location_result.entries_added == 1U;
    const openmeta::MetadataAuthoringEntry capture_rational_entry {
        openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                             "SubjectDistance"),
        openmeta::make_value_view_text("Infinity",
                                       openmeta::TextEncoding::Ascii),
    };
    openmeta::MetaStore capture_rational_source;
    const auto capture_rational_authored = openmeta::create_metadata_store(
        std::span(&capture_rational_entry, 1U), &capture_rational_source);
    openmeta::MetaStore capture_rational_output;
    const auto capture_rational_result
        = openmeta::translate_xmp_capture_rational_metadata(
            capture_rational_source, {}, &capture_rational_output);
    const bool capture_rational_contract_matches
        = capture_rational_authored.ok()
          && capture_rational_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && capture_rational_result.entries_added == 1U;
    const openmeta::MetadataAuthoringEntry flash_entry {
        openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                             "Flash"),
        openmeta::make_value_view_text("95", openmeta::TextEncoding::Ascii),
    };
    openmeta::MetaStore flash_source;
    const auto flash_authored
        = openmeta::create_metadata_store(std::span(&flash_entry, 1U),
                                          &flash_source);
    openmeta::MetaStore flash_output;
    const auto flash_result
        = openmeta::translate_xmp_flash_metadata(flash_source, {},
                                                 &flash_output);
    const bool flash_contract_matches
        = flash_authored.ok()
          && flash_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && flash_result.entries_added == 1U;
    const openmeta::MetadataAuthoringEntry light_source_entry {
        openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                             "LightSource"),
        openmeta::make_value_view_text("25", openmeta::TextEncoding::Ascii),
    };
    openmeta::MetaStore light_source_source;
    const auto light_source_authored
        = openmeta::create_metadata_store(std::span(&light_source_entry, 1U),
                                          &light_source_source);
    openmeta::MetaStore light_source_output;
    const auto light_source_result
        = openmeta::translate_xmp_light_source_metadata(light_source_source, {},
                                                        &light_source_output);
    const bool light_source_contract_matches
        = light_source_authored.ok()
          && light_source_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && light_source_result.entries_added == 1U;
    const std::array<openmeta::MetadataAuthoringEntry, 3> sensitivity_entries
        = { {
            { openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                                   "PhotographicSensitivity"),
              openmeta::make_value_view_text("65535",
                                             openmeta::TextEncoding::Ascii) },
            { openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                                   "SensitivityType"),
              openmeta::make_value_view_text("3",
                                             openmeta::TextEncoding::Ascii) },
            { openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                                   "ISOSpeed"),
              openmeta::make_value_view_text("102400",
                                             openmeta::TextEncoding::Ascii) },
        } };
    openmeta::MetaStore sensitivity_source;
    const auto sensitivity_authored
        = openmeta::create_metadata_store(sensitivity_entries,
                                          &sensitivity_source);
    const auto sensitivity_result
        = openmeta::translate_xmp_sensitivity_metadata(sensitivity_source, {},
                                                       &sensitivity_source);
    const bool sensitivity_contract_matches
        = sensitivity_authored.ok()
          && sensitivity_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && sensitivity_result.entries_added == 3U
          && sensitivity_result.groups_translated == 1U;
    const std::array<openmeta::MetadataAuthoringEntry, 6> camera_text_entries
        = { {
            { openmeta::make_xmp_property_key_view(
                  "http://ns.adobe.com/exif/1.0/", "SpectralSensitivity"),
              openmeta::make_value_view_text("ASCII 001",
                                             openmeta::TextEncoding::Utf8) },
            { openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                                   "CameraOwnerName"),
              openmeta::make_value_view_text("ASCII 001",
                                             openmeta::TextEncoding::Utf8) },
            { openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                                   "BodySerialNumber"),
              openmeta::make_value_view_text("ASCII 001",
                                             openmeta::TextEncoding::Utf8) },
            { openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                                   "LensMake"),
              openmeta::make_value_view_text("ASCII 001",
                                             openmeta::TextEncoding::Utf8) },
            { openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                                   "LensModel"),
              openmeta::make_value_view_text("ASCII 001",
                                             openmeta::TextEncoding::Utf8) },
            { openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                                   "LensSerialNumber"),
              openmeta::make_value_view_text("ASCII 001",
                                             openmeta::TextEncoding::Utf8) },
        } };
    openmeta::MetaStore camera_text_source;
    const auto camera_text_authored
        = openmeta::create_metadata_store(camera_text_entries,
                                          &camera_text_source);
    const auto camera_text_result
        = openmeta::translate_xmp_camera_text_metadata(camera_text_source, {},
                                                       &camera_text_source);
    const bool camera_text_contract_matches
        = camera_text_authored.ok()
          && camera_text_result.status
                 == openmeta::MetadataTechnicalTranslationStatus::Ok
          && camera_text_result.entries_added == 6U
          && camera_text_result.groups_translated == 6U;
    const std::array<openmeta::MetadataAuthoringEntry, 5> apex_entries = { {
        { openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "ShutterSpeedValue"),
          openmeta::make_value_view_text("-7/3",
                                         openmeta::TextEncoding::Ascii) },
        { openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "ApertureValue"),
          openmeta::make_value_view_text("0", openmeta::TextEncoding::Ascii) },
        { openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "BrightnessValue"),
          openmeta::make_value_view_text("-0.5",
                                         openmeta::TextEncoding::Ascii) },
        { openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "ExposureBiasValue"),
          openmeta::make_value_view_text("1/3", openmeta::TextEncoding::Ascii) },
        { openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "MaxApertureValue"),
          openmeta::make_value_view_text("4294967295/2",
                                         openmeta::TextEncoding::Ascii) },
    } };
    openmeta::MetaStore apex_source;
    const auto apex_authored = openmeta::create_metadata_store(apex_entries,
                                                               &apex_source);
    const auto apex_result
        = openmeta::translate_xmp_apex_metadata(apex_source, {}, &apex_source);
    const bool apex_contract_matches
        = apex_authored.ok()
          && openmeta::kMetadataApexTranslationContractVersion == 1U
          && apex_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && apex_result.entries_added == 5U
          && apex_result.groups_translated == 5U
          && openmeta::validate_store(apex_source).ok();
    constexpr std::array<uint16_t, 4> subject_area = { 0U, 65535U, 12U, 34U };
    const std::array<openmeta::MetadataAuthoringEntry, 5> spatial_entries = { {
        { openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "FocalPlaneXResolution"),
          openmeta::make_value_view_text("10000/3",
                                         openmeta::TextEncoding::Ascii) },
        { openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "FocalPlaneYResolution"),
          openmeta::make_value_view_text("2500",
                                         openmeta::TextEncoding::Ascii) },
        { openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "FocalPlaneResolutionUnit"),
          openmeta::make_value_view_text("cm", openmeta::TextEncoding::Ascii) },
        { openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "SubjectArea"),
          openmeta::make_value_view_array(openmeta::MetaElementType::U16,
                                          std::as_bytes(std::span(subject_area)),
                                          4U) },
        { openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "SubjectLocation"),
          openmeta::make_value_view_array(
              openmeta::MetaElementType::U16,
              std::as_bytes(std::span(subject_area.data(), 2U)), 2U) },
    } };
    openmeta::MetaStore spatial_source;
    const auto spatial_authored
        = openmeta::create_metadata_store(spatial_entries, &spatial_source);
    const auto spatial_result
        = openmeta::translate_xmp_capture_spatial_metadata(spatial_source, {},
                                                           &spatial_source);
    const bool spatial_contract_matches
        = spatial_authored.ok()
          && openmeta::kMetadataCaptureSpatialTranslationContractVersion == 1U
          && spatial_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && spatial_result.entries_added == 5U
          && spatial_result.groups_translated == 3U
          && openmeta::validate_store(spatial_source).ok();
    const std::array<openmeta::MetadataAuthoringEntry, 6> capture_sync_entries
        = { {
            { openmeta::make_exif_tag_key_view("exififd", 0x829dU),
              openmeta::make_value_view_urational(17U, 6U) },
            { openmeta::make_exif_tag_key_view("exififd", 0x920aU),
              openmeta::make_value_view_urational(50U, 3U) },
            { openmeta::make_exif_tag_key_view("exififd", 0xa404U),
              openmeta::make_value_view_urational(1U, 3U) },
            { openmeta::make_exif_tag_key_view("exififd", 0x8827U),
              openmeta::make_value_view_u16(400U) },
            { openmeta::make_exif_tag_key_view("exififd", 0x8830U),
              openmeta::make_value_view_u16(0U) },
            { openmeta::make_xmp_property_key_view(
                  "http://ns.adobe.com/exif/1.0/", "ISO"),
              openmeta::make_value_view_text("100",
                                             openmeta::TextEncoding::Ascii) },
        } };
    openmeta::MetaStore capture_sync_source;
    const auto capture_sync_authored
        = openmeta::create_metadata_store(capture_sync_entries,
                                          &capture_sync_source);
    openmeta::XmpPortableOptions capture_sync_options;
    capture_sync_options.include_existing_xmp = true;
    capture_sync_options.conflict_policy
        = openmeta::XmpConflictPolicy::ExistingWins;
    capture_sync_options.existing_standard_namespace_policy
        = openmeta::XmpExistingStandardNamespacePolicy::CanonicalizeManaged;
    std::array<std::byte, 4096> capture_sync_bytes {};
    const auto capture_sync_dumped
        = openmeta::dump_xmp_portable(capture_sync_source, capture_sync_bytes,
                                      capture_sync_options);
    const std::string_view capture_sync_packet(reinterpret_cast<const char*>(
                                                   capture_sync_bytes.data()),
                                               capture_sync_dumped.written);
    const bool capture_sync_contract_matches
        = capture_sync_authored.ok()
          && capture_sync_dumped.status == openmeta::XmpDumpStatus::Ok
          && capture_sync_packet.find("<exif:FNumber>17/6</exif:FNumber>")
                 != std::string_view::npos
          && capture_sync_packet.find(
                 "<exif:FocalLength>50/3</exif:FocalLength>")
                 != std::string_view::npos
          && capture_sync_packet.find(
                 "<exif:DigitalZoomRatio>1/3</exif:DigitalZoomRatio>")
                 != std::string_view::npos
          && capture_sync_packet.find(
                 "<exifEX:PhotographicSensitivity>400</exifEX:PhotographicSensitivity>")
                 != std::string_view::npos
          && capture_sync_packet.find("<exif:ISO>") == std::string_view::npos;
    openmeta::MetaStore typed_source;
    typed_source.finalize();
    std::array<openmeta::MetadataTypedEditingOperation, 7> typed_operations {};
    typed_operations[0].entry.key
        = openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "FileSource");
    typed_operations[0].entry.value = openmeta::make_value_view_u16(2U);
    typed_operations[1].entry.key
        = openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                               "WaterDepth");
    typed_operations[1].entry.value = openmeta::make_value_view_srational(-7,
                                                                          -1);
    typed_operations[2].entry.key
        = openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                               "Gamma");
    typed_operations[2].entry.value = openmeta::make_value_view_urational(0U,
                                                                          7U);
    typed_operations[3].entry.key
        = openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                               "CompositeImage");
    typed_operations[3].entry.value = openmeta::make_value_view_u16(2U);
    typed_operations[4].entry.key
        = openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "CFAPattern/Columns");
    typed_operations[4].entry.value = openmeta::make_value_view_u16(2U);
    typed_operations[5].entry.key
        = openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "CFAPattern/Rows");
    typed_operations[5].entry.value        = openmeta::make_value_view_u16(2U);
    const std::array<uint8_t, 4> cfa_codes = { 0U, 1U, 1U, 2U };
    typed_operations[6].entry.key
        = openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                               "CFAPattern/Values");
    typed_operations[6].entry.value = openmeta::make_value_view_array(
        openmeta::MetaElementType::U8, std::as_bytes(std::span(cfa_codes)), 4U);
    const auto typed_edited = openmeta::edit_metadata_typed(typed_source,
                                                            typed_operations,
                                                            &typed_source);
    const auto additional_result
        = openmeta::translate_xmp_capture_additional_metadata(typed_source, {},
                                                              &typed_source);
    const auto environment_result
        = openmeta::translate_xmp_environment_metadata(typed_source, {},
                                                       &typed_source);
    const auto encoding_result
        = openmeta::translate_xmp_image_encoding_metadata(typed_source, {},
                                                          &typed_source);
    const auto composite_result
        = openmeta::translate_xmp_composite_metadata(typed_source, {},
                                                     &typed_source);
    const auto structured_result
        = openmeta::translate_xmp_structured_capture_metadata(typed_source, {},
                                                              &typed_source);
    const auto typed_dumped = openmeta::dump_xmp_portable(typed_source,
                                                          capture_sync_bytes,
                                                          capture_sync_options);
    const std::string_view typed_packet(reinterpret_cast<const char*>(
                                            capture_sync_bytes.data()),
                                        typed_dumped.written);
    const bool typed_contract_matches
        = typed_edited.ok()
          && structured_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && structured_result.entries_added == 1U
          && typed_packet.find("<exif:CFAPattern rdf:parseType=\"Resource\">")
                 != std::string_view::npos
          && encoding_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && composite_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && typed_packet.find("<exifEX:Gamma>0/7</exifEX:Gamma>")
                 != std::string_view::npos
          && typed_packet.find(
                 "<exifEX:CompositeImage>2</exifEX:CompositeImage>")
                 != std::string_view::npos
          && additional_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && environment_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && openmeta::validate_store(typed_source).ok()
          && typed_dumped.status == openmeta::XmpDumpStatus::Ok
          && typed_packet.find("<exif:FileSource>2</exif:FileSource>")
                 != std::string_view::npos
          && typed_packet.find("<exifEX:WaterDepth>-7/-1</exifEX:WaterDepth>")
                 != std::string_view::npos;
    constexpr std::array<openmeta::URational, 4> identity_lens = {
        openmeta::URational { 24U, 1U }, { 70U, 1U }, { 14U, 5U }, { 0U, 0U }
    };
    const std::array<openmeta::MetadataAuthoringEntry, 2> identity_entries = {
        { { openmeta::make_xmp_property_key_view("http://cipa.jp/exif/1.0/",
                                                 "LensSpecification"),
            openmeta::make_value_view_array(
                openmeta::MetaElementType::URational,
                std::as_bytes(std::span(identity_lens)), 4U) },
          { openmeta::make_xmp_property_key_view("http://ns.adobe.com/exif/1.0/",
                                                 "ImageUniqueID"),
            openmeta::make_value_view_text("00112233445566778899aAbBcCdDeEfF",
                                           openmeta::TextEncoding::Ascii) } }
    };
    openmeta::MetaStore identity_source;
    const auto identity_authored
        = openmeta::create_metadata_store(identity_entries, &identity_source);
    const auto identity_result
        = openmeta::translate_xmp_identity_metadata(identity_source, {},
                                                    &identity_source);
    const bool identity_contract_matches
        = identity_authored.ok()
          && identity_result.status
                 == openmeta::MetadataCaptureTranslationStatus::Ok
          && identity_result.entries_added == 2U
          && identity_result.groups_translated == 2U;
    const openmeta::MetadataAuthoringResult authoring
        = openmeta::create_metadata_store(
            std::span<const openmeta::MetadataAuthoringEntry>(&orientation, 1U),
            &authored);
    const openmeta::MetadataValidationResult validation
        = openmeta::validate_store(authored);
    const openmeta::ExifTiffSerializeResult measured
        = openmeta::serialize_exif_tiff(authored, {});
    std::vector<std::byte> exif(static_cast<size_t>(measured.needed));
    const openmeta::ExifTiffSerializeResult serialized
        = openmeta::serialize_exif_tiff(authored, exif);
    const bool authoring_contract_matches
        = openmeta::kMetadataAuthoringContractVersion == 1U
          && openmeta::kMetadataValidationContractVersion == 1U
          && openmeta::kExifTiffSerializeContractVersion == 1U && authoring.ok()
          && validation.ok()
          && measured.status
                 == openmeta::ExifTiffSerializeStatus::OutputTruncated
          && measured.needed != 0U && serialized.ok()
          && serialized.written == measured.needed;
    openmeta::MetadataPatchRequest patch_request;
    patch_request.key = openmeta::make_exif_tag_key_view("ifd0", 0x0112U);
    patch_request.expected.kind      = openmeta::MetaValueKind::Scalar;
    patch_request.expected.elem_type = openmeta::MetaElementType::U16;
    patch_request.expected.count     = 1U;
    openmeta::MetadataPatchRequest xmp_patch_request;
    xmp_patch_request.key
        = openmeta::make_xmp_property_key_view("http://ns.adobe.com/tiff/1.0/",
                                               "Orientation");
    xmp_patch_request.escaped_width = 1U;
    const std::array patch_requests = { patch_request, xmp_patch_request };
    std::array<openmeta::MetadataPatchHandle, 2> patch_handles;
    openmeta::PreparedMetadataPatchPlan patch_plan;
    const openmeta::MetadataPatchResult patch_prepared
        = openmeta::prepare_metadata_patch_plan(authored, patch_requests,
                                                { .plan_id = 1U },
                                                patch_handles, &patch_plan);
    openmeta::PreparedMetadataPatchInstance patch_instance;
    const openmeta::MetadataPatchResult patch_instance_created
        = openmeta::create_prepared_metadata_patch_instance(patch_plan,
                                                            &patch_instance);
    const std::array<openmeta::MetadataPatchUpdate, 2> patch_updates = { {
        { patch_handles[0], openmeta::make_value_view_u16(3U) },
        { patch_handles[1],
          openmeta::make_value_view_text("3", openmeta::TextEncoding::Utf8) },
    } };
    const openmeta::MetadataPatchResult canonical_patched
        = openmeta::patch_prepared_metadata_instance(&patch_instance,
                                                     patch_updates);
    const bool canonical_patch_contract_matches
        = openmeta::metadata_patch_contract_version()
              == openmeta::kMetadataPatchContractVersion
          && patch_prepared.ok() && patch_instance_created.ok()
          && canonical_patched.ok() && canonical_patched.patched_handles == 2U
          && patch_plan.valid() && patch_instance.valid()
          && patch_plan.payload(openmeta::MetadataPatchPayload::ExifTiff).size()
                 == patch_instance
                        .payload(openmeta::MetadataPatchPayload::ExifTiff)
                        .size()
          && !patch_instance.payload(openmeta::MetadataPatchPayload::Xmp).empty()
          && patch_plan.payload(openmeta::MetadataPatchPayload::Xmp).size()
                 == patch_instance.payload(openmeta::MetadataPatchPayload::Xmp)
                        .size();
    openmeta::PreparedTransferHandoff handoff;
    openmeta::PreparedTransferHandoffInstance instance;
    openmeta::PreparedTransferHandoffTimePatchFieldView field;
    const openmeta::PreparedTransferHandoffResult created
        = openmeta::create_prepared_transfer_handoff_instance(handoff,
                                                              &instance);
    const openmeta::PreparedTransferHandoffPatchResult described
        = openmeta::prepared_transfer_handoff_instance_time_patch_field(
            instance, openmeta::TimePatchField::DateTime, &field);
    const openmeta::PreparedTransferHandoffPatchResult patched
        = openmeta::patch_prepared_transfer_handoff_instance(
            &instance, std::span<const openmeta::TimePatchView> {});
    openmeta::PreparedTransferHandoffOperationView operation;
    const openmeta::PreparedTransferHandoffResult resolved
        = openmeta::prepared_transfer_handoff_instance_operation(instance, 0U,
                                                                 &operation);
    const openmeta::PreparedTransferHandoffResult replayed
        = openmeta::replay_prepared_transfer_handoff_instance(instance, nullptr,
                                                              nullptr);
    return line1.empty() || line2.empty() || !profile_matches
                   || !handoff_contract_matches || !instance_contract_matches
                   || !translation_contract_matches
                   || !descriptive_translation_contract_matches
                   || !location_contract_matches || !editorial_contract_matches
                   || !iptc_contract_matches || !gps_contract_matches
                   || !structured_location_contract_matches
                   || !navigation_contract_matches
                   || !destination_contract_matches || !quality_contract_matches
                   || !gps_text_contract_matches || !setting_contract_matches
                   || !capture_rational_contract_matches
                   || !flash_contract_matches || !light_source_contract_matches
                   || !sensitivity_contract_matches
                   || !camera_text_contract_matches || !apex_contract_matches
                   || !spatial_contract_matches || !identity_contract_matches
                   || !capture_sync_contract_matches || !typed_contract_matches
                   || !location_creation_contract_matches
                   || !authoring_contract_matches
                   || !canonical_patch_contract_matches || handoff.valid()
                   || instance.valid()
                   || created.code
                          != openmeta::PreparedTransferHandoffCode::InvalidState
                   || described.code
                          != openmeta::PreparedTransferHandoffPatchCode::InvalidState
                   || patched.code
                          != openmeta::PreparedTransferHandoffPatchCode::InvalidState
                   || resolved.code
                          != openmeta::PreparedTransferHandoffCode::InvalidState
                   || replayed.code
                          != openmeta::PreparedTransferHandoffCode::NullReplayCallback
               ? 1
               : 0;
}
