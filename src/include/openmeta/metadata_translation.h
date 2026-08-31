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

/// Experimental reverse technical-EXIF translation contract version.
inline constexpr uint32_t kMetadataTechnicalTranslationContractVersion = 1U;

inline constexpr uint32_t kMetadataTechnicalTranslationMaxAddedEntries = 6U;
inline constexpr uint32_t kMetadataTechnicalTranslationMaxOperations   = 1024U;
inline constexpr uint32_t kMetadataTechnicalTranslationMaxTextBytesPerProperty
    = 4096U;
inline constexpr uint64_t kMetadataTechnicalTranslationMaxTotalTextBytes
    = 16ULL * 1024ULL;

/// Which exact XMP properties are eligible as reverse technical sources.
enum class MetadataTechnicalTranslationSourceMode : uint8_t {
    /// Translate only entries marked Dirty, including dirty tombstones.
    DirtyOnly,
    /// Translate active clean or dirty entries; tombstones still require Dirty.
    All,
};

/// How an existing native EXIF group is reconciled.
enum class MetadataTechnicalTranslationConflictPolicy : uint8_t {
    PreserveExisting,
    FailOnConflict,
    ReplaceExisting,
};

/// Exact technical source mapping associated with a result or failure.
enum class MetadataTechnicalTranslationMapping : uint8_t {
    None,
    XmpModifyDate,
    TiffMake,
    TiffModel,
    XmpCreatorTool,
};

/// Caller-selected bounded reverse technical mappings.
struct MetadataTechnicalTranslationOptions final {
    MetadataTechnicalTranslationSourceMode source_mode
        = MetadataTechnicalTranslationSourceMode::DirtyOnly;
    MetadataTechnicalTranslationConflictPolicy conflict_policy
        = MetadataTechnicalTranslationConflictPolicy::FailOnConflict;

    /// xmp:ModifyDate -> IFD0 DateTime plus exact EXIF companions.
    bool modify_date_to_exif_datetime = true;
    /// tiff:Make -> IFD0 Make.
    bool make_to_exif_make = true;
    /// tiff:Model -> IFD0 Model.
    bool model_to_exif_model = true;
    /// xmp:CreatorTool -> IFD0 Software.
    bool creator_tool_to_exif_software = true;

    uint32_t max_added_entries = kMetadataTechnicalTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataTechnicalTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataTechnicalTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataTechnicalTranslationMaxTotalTextBytes;
};

enum class MetadataTechnicalTranslationStatus : uint8_t {
    Ok,
    NullOutput,
    SourceNotFinalized,
    InvalidOptions,
    AmbiguousSource,
    InvalidSourceValue,
    InvalidDateTime,
    UnsupportedPrecision,
    NonAsciiSource,
    ValueTooLong,
    SourceLimitExceeded,
    NativeConflict,
    EntryLimitExceeded,
    OperationLimitExceeded,
    InternalError,
};

/// Transactional result details for one reverse technical translation.
struct MetadataTechnicalTranslationResult final {
    MetadataTechnicalTranslationStatus status
        = MetadataTechnicalTranslationStatus::Ok;
    MetadataTechnicalTranslationMapping failed_mapping
        = MetadataTechnicalTranslationMapping::None;
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
 * \brief Translate exact standard XMP/TIFF technical properties into EXIF.
 *
 * Supported mappings are xmp:ModifyDate, tiff:Make, tiff:Model, and
 * xmp:CreatorTool. ModifyDate requires a full time and preserves up to nine
 * fractional digits and a timezone through SubSecTime and OffsetTime. Text
 * mappings require non-empty 7-bit ASCII without embedded NUL bytes.
 *
 * Each mapping reconciles independently. The source store is immutable and
 * the output is replaced only after every selected mapping and resource limit
 * succeeds. Exact namespaces and property paths are required, and duplicate
 * eligible sources are ambiguous.
 */
MetadataTechnicalTranslationResult
translate_xmp_technical_metadata(
    const MetaStore& source, const MetadataTechnicalTranslationOptions& options,
    MetaStore* out_store);

const char*
metadata_technical_translation_status_name(
    MetadataTechnicalTranslationStatus status) noexcept;

const char*
metadata_technical_translation_mapping_name(
    MetadataTechnicalTranslationMapping mapping) noexcept;

/// Experimental reverse capture-EXIF translation contract version.
inline constexpr uint32_t kMetadataCaptureTranslationContractVersion = 1U;

inline constexpr uint32_t kMetadataCaptureTranslationMaxAddedEntries = 5U;
inline constexpr uint32_t kMetadataCaptureTranslationMaxOperations   = 1024U;
inline constexpr uint32_t kMetadataCaptureTranslationMaxTextBytesPerProperty
    = 128U;
inline constexpr uint64_t kMetadataCaptureTranslationMaxTotalTextBytes = 640U;

/// Which exact XMP properties are eligible as reverse capture sources.
enum class MetadataCaptureTranslationSourceMode : uint8_t {
    /// Translate only entries marked Dirty, including dirty tombstones.
    DirtyOnly,
    /// Translate active clean or dirty entries; tombstones still require Dirty.
    All,
};

/// How an existing native EXIF capture field is reconciled.
enum class MetadataCaptureTranslationConflictPolicy : uint8_t {
    PreserveExisting,
    FailOnConflict,
    ReplaceExisting,
};

/// Exact capture source mapping associated with a result or failure.
enum class MetadataCaptureTranslationMapping : uint8_t {
    None,
    XmpExposureTime,
    XmpFNumber,
    XmpIso,
    XmpFocalLength,
    XmpExposureCompensation,
};

/// Caller-selected bounded reverse capture mappings.
struct MetadataCaptureTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;

    /// exif:ExposureTime -> ExifIFD ExposureTime RATIONAL.
    bool exposure_time_to_exif = true;
    /// exif:FNumber -> ExifIFD FNumber RATIONAL.
    bool f_number_to_exif = true;
    /// exif:ISO or standard ISOSpeedRatings -> ExifIFD SHORT.
    bool iso_to_exif = true;
    /// exif:FocalLength -> ExifIFD FocalLength RATIONAL.
    bool focal_length_to_exif = true;
    /// exif:ExposureCompensation or ExposureBiasValue -> SRATIONAL.
    bool exposure_compensation_to_exif = true;

    uint32_t max_added_entries = kMetadataCaptureTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes = kMetadataCaptureTranslationMaxTotalTextBytes;
};

enum class MetadataCaptureTranslationStatus : uint8_t {
    Ok,
    NullOutput,
    SourceNotFinalized,
    InvalidOptions,
    AmbiguousSource,
    InvalidSourceValue,
    InvalidNumericValue,
    ValueOutOfRange,
    ValueTooLong,
    SourceLimitExceeded,
    NativeConflict,
    EntryLimitExceeded,
    OperationLimitExceeded,
    InternalError,
};

/// Transactional result details for one reverse capture translation.
struct MetadataCaptureTranslationResult final {
    MetadataCaptureTranslationStatus status
        = MetadataCaptureTranslationStatus::Ok;
    MetadataCaptureTranslationMapping failed_mapping
        = MetadataCaptureTranslationMapping::None;
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
 * \brief Translate exact standard or OpenMeta-portable XMP capture properties
 * into native EXIF scalar fields.
 *
 * Unsigned rational sources accept typed scalar values or full decimal,
 * scientific-decimal, integer, and `numerator/denominator` text. Focal length
 * additionally accepts the portable ` mm` suffix. Exposure compensation uses
 * a signed rational. ISO is restricted to one integer in the EXIF SHORT range.
 * Text conversion is exact after rational reduction; values that cannot fit
 * the native EXIF representation fail rather than being approximated.
 *
 * Portable `ISO` and `ExposureCompensation` aliases and standard
 * `ISOSpeedRatings` and `ExposureBiasValue` paths target the same singleton;
 * multiple eligible aliases are ambiguous. Each mapping reconciles
 * independently and the output is replaced only after every selected mapping
 * and resource limit succeeds.
 */
MetadataCaptureTranslationResult
translate_xmp_capture_metadata(const MetaStore& source,
                               const MetadataCaptureTranslationOptions& options,
                               MetaStore* out_store);

const char*
metadata_capture_translation_status_name(
    MetadataCaptureTranslationStatus status) noexcept;

const char*
metadata_capture_translation_mapping_name(
    MetadataCaptureTranslationMapping mapping) noexcept;

/// Experimental reverse descriptive-metadata translation contract version.
inline constexpr uint32_t kMetadataDescriptiveTranslationContractVersion = 1U;

inline constexpr uint32_t kMetadataDescriptiveTranslationMaxSourceProperties
    = 1024U;
inline constexpr uint32_t kMetadataDescriptiveTranslationMaxAddedEntries = 1025U;
inline constexpr uint32_t kMetadataDescriptiveTranslationMaxOperations = 4096U;
inline constexpr uint64_t kMetadataDescriptiveTranslationMaxTotalTextBytes
    = 8ULL * 1024ULL * 1024ULL;

/// Which XMP entries are eligible as reverse descriptive sources.
enum class MetadataDescriptiveTranslationSourceMode : uint8_t {
    /// Translate a property group only when at least one member is Dirty.
    DirtyOnly,
    /// Translate active clean or dirty entries; tombstones still require Dirty.
    All,
};

/// How an existing native IPTC-IIM dataset group is reconciled.
enum class MetadataDescriptiveTranslationConflictPolicy : uint8_t {
    PreserveExisting,
    FailOnConflict,
    ReplaceExisting,
};

/// Exact descriptive source mapping associated with a result or failure.
enum class MetadataDescriptiveTranslationMapping : uint8_t {
    None,
    DcTitle,
    DcDescription,
    DcCreator,
    DcSubject,
    DcRights,
    PhotoshopCredit,
    PhotoshopSource,
};

/// Caller-selected bounded reverse descriptive mappings.
struct MetadataDescriptiveTranslationOptions final {
    MetadataDescriptiveTranslationSourceMode source_mode
        = MetadataDescriptiveTranslationSourceMode::DirtyOnly;
    MetadataDescriptiveTranslationConflictPolicy conflict_policy
        = MetadataDescriptiveTranslationConflictPolicy::FailOnConflict;

    bool title_to_iptc_object_name   = true;
    bool description_to_iptc_caption = true;
    bool creators_to_iptc_bylines    = true;
    bool keywords_to_iptc_keywords   = true;
    bool copyright_to_iptc_copyright = true;
    bool credit_to_iptc_credit       = true;
    bool source_to_iptc_source       = true;

    uint32_t max_source_properties
        = kMetadataDescriptiveTranslationMaxSourceProperties;
    uint32_t max_added_entries = kMetadataDescriptiveTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataDescriptiveTranslationMaxOperations;
    uint64_t max_total_text_bytes
        = kMetadataDescriptiveTranslationMaxTotalTextBytes;
};

enum class MetadataDescriptiveTranslationStatus : uint8_t {
    Ok,
    NullOutput,
    SourceNotFinalized,
    InvalidOptions,
    SourceLimitExceeded,
    AmbiguousSource,
    InvalidSourceValue,
    ValueTooLong,
    NativeConflict,
    NativeEncodingConflict,
    EntryLimitExceeded,
    OperationLimitExceeded,
    InternalError,
};

/// Transactional result details for one reverse descriptive translation.
struct MetadataDescriptiveTranslationResult final {
    MetadataDescriptiveTranslationStatus status
        = MetadataDescriptiveTranslationStatus::Ok;
    MetadataDescriptiveTranslationMapping failed_mapping
        = MetadataDescriptiveTranslationMapping::None;
    EntryId failed_source_entry = kInvalidEntryId;
    uint32_t source_properties  = 0U;
    uint32_t groups_translated  = 0U;
    uint32_t groups_preserved   = 0U;
    uint32_t groups_unchanged   = 0U;
    uint32_t entries_added      = 0U;
    uint32_t entries_updated    = 0U;
    uint32_t entries_removed    = 0U;
    bool utf8_charset_added     = false;
};

/**
 * \brief Translate exact standard XMP descriptive properties into IPTC-IIM.
 *
 * Supported mappings are dc:title, dc:description, dc:creator, dc:subject,
 * dc:rights, photoshop:Credit, and photoshop:Source. Default-language
 * singleton paths and indexed creator/subject items are required exactly.
 * Repeated values retain XMP index order.
 *
 * IPTC-IIM dataset byte limits are enforced without truncation. Non-ASCII
 * UTF-8 adds CodedCharacterSet ESC % G only when existing active IPTC bytes
 * are ASCII or are replaced by this transaction; otherwise the call fails
 * with NativeEncodingConflict. The output is replaced only after all selected
 * mappings and resource limits succeed.
 */
MetadataDescriptiveTranslationResult
translate_xmp_descriptive_metadata(
    const MetaStore& source,
    const MetadataDescriptiveTranslationOptions& options, MetaStore* out_store);

const char*
metadata_descriptive_translation_status_name(
    MetadataDescriptiveTranslationStatus status) noexcept;

const char*
metadata_descriptive_translation_mapping_name(
    MetadataDescriptiveTranslationMapping mapping) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
