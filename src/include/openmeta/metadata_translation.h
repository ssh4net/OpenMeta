// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstdint>

/**
 * \file metadata_translation.h
 * \brief Bounded explicit translation between metadata families.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Experimental XMP creation-date translation contract version.
inline constexpr uint32_t kMetadataDateTranslationContractVersion = 1U;

/// Hard limit for native entries added by one date-translation call.
inline constexpr uint32_t kMetadataDateTranslationMaxAddedEntries = 16U;
inline constexpr uint32_t kMetadataDateTranslationMaxOperations   = 1024U;

/// Which XMP entries are eligible as reverse-translation sources.
enum class MetadataDateTranslationSourceMode : uint8_t {
    /// Translate only entries marked Dirty, including dirty tombstones.
    DirtyOnly,
    /// Translate active clean or dirty entries; tombstones still require Dirty.
    All,
};

/// How an existing native EXIF/IPTC date group is reconciled.
enum class MetadataDateTranslationConflictPolicy : uint8_t {
    /// Keep the complete existing native group when any member is present.
    PreserveExisting,
    /// Require the native group to be absent or already exactly equivalent.
    FailOnConflict,
    /// Replace the native group and tombstone stale or duplicate members.
    ReplaceExisting,
};

/// Exact source mapping associated with a result or failure.
enum class MetadataDateTranslationMapping : uint8_t {
    None,
    XmpCreateDate,
    PhotoshopDateCreated,
    XmpDateTimeOriginal,
};

/// Caller-selected bounded reverse-date mappings.
struct MetadataDateTranslationOptions final {
    MetadataDateTranslationSourceMode source_mode
        = MetadataDateTranslationSourceMode::DirtyOnly;
    MetadataDateTranslationConflictPolicy conflict_policy
        = MetadataDateTranslationConflictPolicy::FailOnConflict;

    /// xmp:CreateDate -> EXIF DateTimeDigitized plus exact companions.
    bool create_date_to_exif_digitized = true;
    /// xmp:CreateDate -> IPTC DigitalCreationDate/Time.
    bool create_date_to_iptc_digital_creation = true;
    /// photoshop:DateCreated -> IPTC DateCreated/TimeCreated.
    bool date_created_to_iptc_created = true;
    /// exif:DateTimeOriginal XMP -> native EXIF DateTimeOriginal companions.
    bool date_time_original_to_exif_original = true;

    uint32_t max_added_entries = kMetadataDateTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataDateTranslationMaxOperations;
};

/// Stable result status for translate_xmp_creation_dates.
enum class MetadataDateTranslationStatus : uint8_t {
    Ok,
    NullOutput,
    SourceNotFinalized,
    InvalidOptions,
    AmbiguousSource,
    InvalidSourceValue,
    InvalidDateTime,
    UnsupportedPrecision,
    NativeConflict,
    EntryLimitExceeded,
    OperationLimitExceeded,
    InternalError,
};

/// Transactional result details for one reverse-date translation.
struct MetadataDateTranslationResult final {
    MetadataDateTranslationStatus status = MetadataDateTranslationStatus::Ok;
    MetadataDateTranslationMapping failed_mapping
        = MetadataDateTranslationMapping::None;
    EntryId failed_source_entry = kInvalidEntryId;
    uint32_t source_properties  = 0U;
    uint32_t groups_translated  = 0U;
    uint32_t groups_preserved   = 0U;
    uint32_t groups_unchanged   = 0U;
    uint32_t entries_added      = 0U;
    uint32_t entries_updated    = 0U;
    uint32_t entries_removed    = 0U;
};

/**
 * \brief Translate exact standard XMP creation-date properties into native
 * EXIF/IPTC entries.
 *
 * The source store is immutable and the output is replaced only after every
 * selected mapping parses and reconciles successfully. Parsing accepts only a
 * full Gregorian `YYYY-MM-DD` date with optional `T` followed by `hh:mm:ss`,
 * up to nine fractional digits, and `Z` or `+/-HH:MM` timezone.
 *
 * EXIF projection requires a time and preserves fractions and timezone through
 * SubSecTime* and OffsetTime* companion tags. IPTC projection supports date
 * only or whole seconds with an optional timezone; fractional seconds return
 * UnsupportedPrecision rather than being truncated. Exact source-property
 * namespaces are required, and duplicate eligible sources are ambiguous.
 *
 * Calls keep no global state and are safe when each call owns its output store.
 */
MetadataDateTranslationResult
translate_xmp_creation_dates(const MetaStore& source,
                             const MetadataDateTranslationOptions& options,
                             MetaStore* out_store);

const char*
metadata_date_translation_status_name(
    MetadataDateTranslationStatus status) noexcept;

const char*
metadata_date_translation_mapping_name(
    MetadataDateTranslationMapping mapping) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
