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
