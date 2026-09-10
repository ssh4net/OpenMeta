// SPDX-License-Identifier: Apache-2.0

#include <openmeta/build_info.h>
#include <openmeta/exif_tiff_patch.h>
#include <openmeta/exif_tiff_serialize.h>
#include <openmeta/host_adoption.h>
#include <openmeta/metadata_authoring.h>
#include <openmeta/metadata_translation.h>
#include <openmeta/prepared_transfer_handoff.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
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
    openmeta::ExifTiffPatchRequest patch_request;
    patch_request.key = openmeta::make_exif_tag_key_view("ifd0", 0x0112U);
    patch_request.expected.kind      = openmeta::MetaValueKind::Scalar;
    patch_request.expected.elem_type = openmeta::MetaElementType::U16;
    patch_request.expected.count     = 1U;
    openmeta::ExifTiffPatchHandle patch_handle;
    openmeta::PreparedExifTiffPatchPlan patch_plan;
    const openmeta::ExifTiffPatchResult patch_prepared
        = openmeta::prepare_exif_tiff_patch_plan(
            authored,
            std::span<const openmeta::ExifTiffPatchRequest>(&patch_request, 1U),
            {}, std::span<openmeta::ExifTiffPatchHandle>(&patch_handle, 1U),
            &patch_plan);
    openmeta::PreparedExifTiffPatchInstance patch_instance;
    const openmeta::ExifTiffPatchResult patch_instance_created
        = openmeta::create_prepared_exif_tiff_patch_instance(patch_plan,
                                                             &patch_instance);
    const openmeta::ExifTiffPatchUpdate patch_update {
        patch_handle,
        openmeta::make_value_view_u16(3U),
    };
    const openmeta::ExifTiffPatchResult canonical_patched
        = openmeta::patch_prepared_exif_tiff_instance(
            &patch_instance,
            std::span<const openmeta::ExifTiffPatchUpdate>(&patch_update, 1U));
    const bool canonical_patch_contract_matches
        = openmeta::exif_tiff_patch_contract_version()
              == openmeta::kExifTiffPatchContractVersion
          && patch_prepared.ok() && patch_instance_created.ok()
          && canonical_patched.ok() && patch_plan.valid()
          && patch_instance.valid()
          && patch_plan.payload().size() == patch_instance.payload().size();
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
