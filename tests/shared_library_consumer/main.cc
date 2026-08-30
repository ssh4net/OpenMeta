// SPDX-License-Identifier: Apache-2.0

#include <openmeta/build_info.h>
#include <openmeta/host_adoption.h>
#include <openmeta/metadata_translation.h>
#include <openmeta/prepared_transfer_handoff.h>

#include <span>
#include <string>

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
                   || handoff.valid() || instance.valid()
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
