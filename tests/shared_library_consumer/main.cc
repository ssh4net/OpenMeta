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
    openmeta::MetaStore authored;
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
