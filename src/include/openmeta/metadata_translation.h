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

struct TransferTargetImageSpec;

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
    XmpSpectralSensitivity,
    XmpCameraOwnerName,
    XmpBodySerialNumber,
    XmpLensMake,
    XmpLensModel,
    XmpLensSerialNumber,
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
    UnsupportedSourceShape,
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

/// Experimental camera/lens/spectral ASCII writeback contract.
inline constexpr uint32_t kMetadataCameraTextTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataCameraTextTranslationMaxAddedEntries = 6U;
inline constexpr uint64_t kMetadataCameraTextTranslationMaxTotalTextBytes
    = 24576U;

struct MetadataCameraTextTranslationOptions final {
    MetadataTechnicalTranslationSourceMode source_mode
        = MetadataTechnicalTranslationSourceMode::DirtyOnly;
    MetadataTechnicalTranslationConflictPolicy conflict_policy
        = MetadataTechnicalTranslationConflictPolicy::FailOnConflict;
    bool spectral_sensitivity_to_exif = true;
    bool camera_owner_name_to_exif    = true;
    bool body_serial_number_to_exif   = true;
    bool lens_make_to_exif            = true;
    bool lens_model_to_exif           = true;
    bool lens_serial_number_to_exif   = true;
    uint32_t max_added_entries = kMetadataCameraTextTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataTechnicalTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataTechnicalTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataCameraTextTranslationMaxTotalTextBytes;
};

/**
 * \brief Translate six independent camera/lens/spectral text fields atomically.
 *
 * Exact exif:SpectralSensitivity and exifEX:CameraOwnerName, BodySerialNumber,
 * LensMake, LensModel and LensSerialNumber map to their ExifIFD ASCII tags.
 * The five exifEX fields also accept historical OpenMeta exif namespace aliases.
 * Inputs must be nonempty printable ASCII (0x20..0x7e), stored as ASCII or UTF-8
 * text. No trimming, Unicode conversion, identity inference or spectral grammar
 * interpretation is performed. Eligible duplicates and structured/indexed forms
 * fail. Missing or disabled sources retain native fields; dirty tombstones
 * remove selected fields under ReplaceExisting. Native equivalence requires
 * ASCII/UTF-8 Text with matching bytes, ignoring native terminal NUL bytes.
 *
 * Each field has its own conflict decision and result group; the entire call
 * commits once, including aliased source/output. Preparation may allocate.
 */
MetadataTechnicalTranslationResult
translate_xmp_camera_text_metadata(
    const MetaStore& source,
    const MetadataCameraTextTranslationOptions& options, MetaStore* out_store);

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
    XmpExposureProgram,
    XmpMeteringMode,
    XmpSensingMethod,
    XmpCustomRendered,
    XmpExposureMode,
    XmpWhiteBalance,
    XmpSceneCaptureType,
    XmpGainControl,
    XmpContrast,
    XmpSaturation,
    XmpSharpness,
    XmpSubjectDistanceRange,
    XmpSubjectDistance,
    XmpDigitalZoomRatio,
    XmpExposureIndex,
    XmpFlashEnergy,
    XmpFlash,
    XmpLightSource,
    XmpSensitivity,
    XmpLensSpecification,
    XmpImageUniqueID,
    XmpShutterSpeedValue,
    XmpApertureValue,
    XmpBrightnessValue,
    XmpMaxApertureValue,
    XmpFocalPlaneResolution,
    XmpSubjectArea,
    XmpSubjectLocation,
    XmpFocalLengthIn35mmFilm,
    XmpFileSource,
    XmpSceneType,
    XmpTemperature,
    XmpHumidity,
    XmpPressure,
    XmpWaterDepth,
    XmpAcceleration,
    XmpCameraElevationAngle,
    XmpGamma,
    XmpCompressedBitsPerPixel,
    XmpComponentsConfiguration,
    XmpCompositeImage,
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
    IncompleteSource,
    UnsupportedSourceShape,
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

inline constexpr uint32_t kMetadataCaptureAdditionalTranslationContractVersion
    = 1U;
inline constexpr uint32_t kMetadataCaptureAdditionalTranslationMaxAddedEntries
    = 3U;
inline constexpr uint64_t kMetadataCaptureAdditionalTranslationMaxTotalTextBytes
    = 384U;

struct MetadataCaptureAdditionalTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    bool focal_length_in_35mm_film_to_exif = true;
    bool file_source_to_exif               = true;
    bool scene_type_to_exif                = true;
    uint32_t max_added_entries
        = kMetadataCaptureAdditionalTranslationMaxAddedEntries;
    uint32_t max_operations = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataCaptureAdditionalTranslationMaxTotalTextBytes;
};

/**
 * Translate exif:FocalLengthIn35mmFilm (or FocalLengthIn35mmFormat), FileSource
 * and SceneType transactionally. Focal length is a SHORT in millimetres, with
 * zero meaning unknown. FileSource codes 0..3 and SceneType code 1 are stored
 * as UNDEFINED count 1, not TIFF BYTE. XMP accepts scalar integers or decimal
 * integer text. No labels, units, rounding or camera-model inference is used.
 * Duplicate aliases and non-scalar paths fail. Dirty tombstones, conflicts,
 * budgets and aliased output follow the capture translation contract.
 */
MetadataCaptureTranslationResult
translate_xmp_capture_additional_metadata(
    const MetaStore& source,
    const MetadataCaptureAdditionalTranslationOptions& options,
    MetaStore* out_store);

inline constexpr uint32_t kMetadataEnvironmentTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataEnvironmentTranslationMaxAddedEntries = 6U;
inline constexpr uint64_t kMetadataEnvironmentTranslationMaxTotalTextBytes
    = 768U;

struct MetadataEnvironmentTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    bool temperature_to_exif            = true;
    bool humidity_to_exif               = true;
    bool pressure_to_exif               = true;
    bool water_depth_to_exif            = true;
    bool acceleration_to_exif           = true;
    bool camera_elevation_angle_to_exif = true;
    uint32_t max_added_entries = kMetadataEnvironmentTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataEnvironmentTranslationMaxTotalTextBytes;
};

/**
 * Translate six scalar exifEX environment properties transactionally. Legacy
 * exif namespace aliases are accepted, but duplicate eligible aliases fail.
 * Temperature (degrees C), WaterDepth (metres) and CameraElevationAngle
 * (degrees) use SRATIONAL. Humidity (percent), Pressure (hPa) and Acceleration
 * (mGal, 10^-5 m/s^2) use RATIONAL. Finite elevation is in [-180, 180).
 * No additional physical range or unit conversion is imposed.
 *
 * Typed rationals, integers, exact decimal/scientific or fraction text are
 * supported. Raw denominator 0xffffffff means unknown; signed fields use -1.
 * The sentinel is recognized before reduction and preserves its numerator.
 * Text "Unknown" uses numerator zero; signed sentinel text accepts n/-1 or
 * n/4294967295. Other finite denominators must be positive. Finite input that
 * would reduce to the reserved denominator fails. No float approximation.
 * Dirty tombstones, conflicts, budgets and aliased output follow the capture
 * contract. Each call is one transaction; shared-object access is host-owned.
 */
MetadataCaptureTranslationResult
translate_xmp_environment_metadata(
    const MetaStore& source,
    const MetadataEnvironmentTranslationOptions& options, MetaStore* out_store);

/// Experimental image-encoding metadata contract; these calls do not alter pixels.
inline constexpr uint32_t kMetadataImageEncodingTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataImageEncodingTranslationMaxAddedEntries = 3U;
inline constexpr uint64_t kMetadataImageEncodingTranslationMaxTotalTextBytes
    = 768U;

struct MetadataImageEncodingTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    bool gamma_to_exif                     = true;
    bool compressed_bits_per_pixel_to_exif = true;
    bool components_configuration_to_exif  = true;
    uint32_t max_added_entries
        = kMetadataImageEncodingTranslationMaxAddedEntries;
    uint32_t max_operations = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataImageEncodingTranslationMaxTotalTextBytes;
};

/**
 * Translate Gamma and CompressedBitsPerPixel as exact nonnegative RATIONALs,
 * and ComponentsConfiguration as four UNDEFINED bytes with codes 0..6.
 * Gamma accepts exifEX and legacy exif; the other two use exif. Components
 * accept a typed unsigned array or four dense one-based indexed properties.
 * The host asserts correspondence to the encoded pixels. No channel reorder,
 * gamma application, compression measurement or pixel inference occurs.
 * Existing target-image transfer filtering remains applicable.
 */
MetadataCaptureTranslationResult
translate_xmp_image_encoding_metadata(
    const MetaStore& source,
    const MetadataImageEncodingTranslationOptions& options,
    MetaStore* out_store);

inline constexpr uint32_t kMetadataCompositeTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataCompositeTranslationMaxAddedEntries = 3U;
inline constexpr uint32_t kMetadataCompositeTranslationMaxExposureValues = 4096U;
inline constexpr uint64_t kMetadataCompositeTranslationMaxTotalTextBytes
    = 1024ULL * 1024ULL;

struct MetadataCompositeTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    uint32_t max_added_entries = kMetadataCompositeTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataCompositeTranslationMaxTotalTextBytes;
    uint32_t max_exposure_values
        = kMetadataCompositeTranslationMaxExposureValues;
};

/**
 * Reconcile CompositeImage and its two companions as one bounded transaction.
 * Canonical sources use exifEX and the full EXIF property names; legacy exif
 * and ExifTool CompositeImageCount/CompositeImageExposureTimes aliases are
 * explicit alternatives. Mixing aliases for one field fails.
 *
 * CompositeImage is 0..3; companions require 2 or 3, and 3 requires both.
 * Counts are total >=2 and used ==0 (unavailable) or 2..total. Exposure times
 * use the CIPA structure: seven summary rationals, NumberOfSequences, optional
 * NumberOfImagesInSequences and dense Values. The list contains m*n values,
 * bounded by max_exposure_values, and agrees with the total count when present.
 * Summary 0/0 means unavailable; m=0 omits n and Values. Finite values are
 * exact nonnegative seconds; no totals, extrema or exposure models are inferred.
 *
 * Any selected member selects the complete source group, including clean
 * companions. Missing native companions are removed only by ReplaceExisting.
 * A deleted CompositeImage with absent/deleted companions removes the group.
 * New A462 byte payloads are little-endian; decoded big-endian bytes retain
 * their raw content and EntryFlags::ValueBigEndian provenance. Typed Set with
 * wire hints resets that flag for the newly supplied little-endian payload.
 */
MetadataCaptureTranslationResult
translate_xmp_composite_metadata(
    const MetaStore& source, const MetadataCompositeTranslationOptions& options,
    MetaStore* out_store);

inline constexpr uint32_t kMetadataApexTranslationContractVersion   = 1U;
inline constexpr uint32_t kMetadataApexTranslationMaxAddedEntries   = 5U;
inline constexpr uint64_t kMetadataApexTranslationMaxTotalTextBytes = 640U;

struct MetadataApexTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    bool shutter_speed_value_to_exif = true;
    bool aperture_value_to_exif      = true;
    bool brightness_value_to_exif    = true;
    bool exposure_bias_value_to_exif = true;
    bool max_aperture_value_to_exif  = true;
    uint32_t max_added_entries       = kMetadataApexTranslationMaxAddedEntries;
    uint32_t max_operations          = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes = kMetadataApexTranslationMaxTotalTextBytes;
};

/**
 * \brief Translate five scalar exif-namespace APEX properties in one transaction.
 *
 * ShutterSpeedValue, BrightnessValue and ExposureBiasValue use SRATIONAL;
 * ApertureValue and MaxApertureValue use nonnegative RATIONAL, including zero.
 * ExposureCompensation is an alias for ExposureBiasValue. All values are direct
 * APEX units, never seconds or f-numbers. No ExposureTime/FNumber inference or
 * cross-field exposure consistency check is performed.
 *
 * Scalar integers, the target rational type, and exact decimal/scientific or
 * fraction text are supported. Denominators must be positive. Brightness text
 * "Unknown" or an explicit signed fraction with numerator -1 denotes unknown,
 * as does a typed SRational with numerator -1. Other brightness inputs are
 * finite; a reduced -1/n is encoded as -2/(2n), or rejected if it cannot fit.
 * Unknown is canonicalized to -1/1. No floating-point approximation is used.
 *
 * Each enabled field reconciles independently. Duplicate eligible aliases and
 * indexed/structured source shapes fail. Dirty tombstones remove native fields
 * under ReplaceExisting. Limits and conflicts precede all mutation, including
 * aliased output. Preparation may allocate; shared-object access is host-owned.
 */
MetadataCaptureTranslationResult
translate_xmp_apex_metadata(const MetaStore& source,
                            const MetadataApexTranslationOptions& options,
                            MetaStore* out_store);

inline constexpr uint32_t kMetadataCaptureSpatialTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataCaptureSpatialTranslationMaxAddedEntries = 5U;
inline constexpr uint64_t kMetadataCaptureSpatialTranslationMaxTotalTextBytes
    = 1152U;

struct MetadataCaptureSpatialTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    bool focal_plane_to_exif      = true;
    bool subject_area_to_exif     = true;
    bool subject_location_to_exif = true;
    uint32_t max_added_entries
        = kMetadataCaptureSpatialTranslationMaxAddedEntries;
    uint32_t max_operations = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataCaptureSpatialTranslationMaxTotalTextBytes;
};

/**
 * \brief Translate focal-plane resolution and subject arrays in one transaction.
 *
 * FocalPlaneXResolution, FocalPlaneYResolution and FocalPlaneResolutionUnit in
 * the exif namespace form one complete group. Resolutions are positive exact
 * unsigned rationals; units are 2/inches or 3/cm. No default unit is inferred.
 * SubjectArea is a U16 array of two (point), three (circle) or four (rectangle)
 * values. SubjectLocation has two U16 coordinates. Each array is independent.
 * Root U16 arrays or dense indexed integer/text members [1]..[N] are accepted.
 * Coordinates and dimensions are preserved, including zero; no rotation,
 * cropping, image-size validation or relationship between arrays is inferred.
 *
 * One eligible dirty member selects its whole group, including clean active
 * companions. Duplicate, sparse, qualified and mixed root/indexed shapes fail.
 * A root tombstone or complete deleted group removes its native targets under
 * ReplaceExisting; partial deletion fails. Conflicts apply to each whole group.
 * Results count three logical groups and up to five native entries. Bounds and
 * all validation precede mutation, including aliased output. Preparation may
 * allocate; synchronization of shared objects is the host's responsibility.
 */
MetadataCaptureTranslationResult
translate_xmp_capture_spatial_metadata(
    const MetaStore& source,
    const MetadataCaptureSpatialTranslationOptions& options,
    MetaStore* out_store);

inline constexpr uint32_t kMetadataIdentityTranslationContractVersion   = 1U;
inline constexpr uint32_t kMetadataIdentityTranslationMaxAddedEntries   = 2U;
inline constexpr uint64_t kMetadataIdentityTranslationMaxTotalTextBytes = 544U;

struct MetadataIdentityTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    bool lens_specification_to_exif = true;
    bool image_unique_id_to_exif    = true;
    uint32_t max_added_entries = kMetadataIdentityTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataIdentityTranslationMaxTotalTextBytes;
};

/**
 * \brief Translate lens specification and image identity in one transaction.
 *
 * exifEX:LensSpecification (or historical exif alias) accepts one four-element
 * URational array or four dense [1]..[4] scalar properties. One dirty member
 * selects the complete lens group, including clean companions. Numeric text
 * accepts exact integer, decimal, scientific and fraction forms. Focal lengths
 * must be positive and ordered; apertures must be positive or exactly 0/0 for
 * unknown. Values are reduced to unsigned 32-bit rational components.
 * exif:ImageUniqueID accepts exactly 32 ASCII hex characters as ASCII/UTF-8 Text.
 * Case and leading zeros are retained; identity is never generated or inferred.
 *
 * Duplicate aliases, mixed root/indexed forms and incomplete arrays fail.
 * A dirty root tombstone or four deleted members removes the lens group;
 * partial member deletion fails. The image ID uses a scalar tombstone.
 * Conflict and resource checks precede all mutations, including aliased output.
 * Preparation may allocate. This is a structural translation contract, not
 * validation of UUID generation, uniqueness or capture-time identity policy.
 */
MetadataCaptureTranslationResult
translate_xmp_identity_metadata(
    const MetaStore& source, const MetadataIdentityTranslationOptions& options,
    MetaStore* out_store);

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

/// Experimental closed-enum capture-settings translation contract.
inline constexpr uint32_t kMetadataCaptureSettingsTranslationContractVersion
    = 1U;
inline constexpr uint32_t kMetadataCaptureSettingsTranslationMaxAddedEntries
    = 12U;
inline constexpr uint64_t kMetadataCaptureSettingsTranslationMaxTotalTextBytes
    = 1536U;

struct MetadataCaptureSettingsTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    /// exif:ExposureProgram -> ExifIFD 0x8822 SHORT.
    bool exposure_program_to_exif = true;
    /// exif:MeteringMode -> ExifIFD 0x9207 SHORT.
    bool metering_mode_to_exif = true;
    /// exif:SensingMethod -> ExifIFD 0xa217 SHORT.
    bool sensing_method_to_exif = true;
    /// exif:CustomRendered -> ExifIFD 0xa401 SHORT.
    bool custom_rendered_to_exif = true;
    /// exif:ExposureMode -> ExifIFD 0xa402 SHORT.
    bool exposure_mode_to_exif = true;
    /// exif:WhiteBalance -> ExifIFD 0xa403 SHORT.
    bool white_balance_to_exif = true;
    /// exif:SceneCaptureType -> ExifIFD 0xa406 SHORT.
    bool scene_capture_type_to_exif = true;
    /// exif:GainControl -> ExifIFD 0xa407 SHORT.
    bool gain_control_to_exif = true;
    /// exif:Contrast -> ExifIFD 0xa408 SHORT.
    bool contrast_to_exif = true;
    /// exif:Saturation -> ExifIFD 0xa409 SHORT.
    bool saturation_to_exif = true;
    /// exif:Sharpness -> ExifIFD 0xa40a SHORT.
    bool sharpness_to_exif = true;
    /// exif:SubjectDistanceRange -> ExifIFD 0xa40c SHORT.
    bool subject_distance_range_to_exif = true;
    uint32_t max_added_entries
        = kMetadataCaptureSettingsTranslationMaxAddedEntries;
    uint32_t max_operations = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataCaptureSettingsTranslationMaxTotalTextBytes;
};

/**
 * \brief Translate twelve exact exif-namespace camera-setting properties to
 * native SHORT singletons using the capture transaction and conflict contract.
 *
 * Accepts scalar integers, unsigned decimal integer text, and exact existing
 * OpenMeta labels for the documented closed code sets. SceneCaptureType also
 * accepts portable "Night scene". Unknown codes, signs in text, floating point,
 * rational/array values, qualifiers, and coercion are excluded. ExposureProgram
 * is limited to 0..8; the read-only Bulb extension is not a writeback code.
 * No exposure, white-balance, rendering, or sensor-state inference is performed.
 * Existing capture option layouts and five numeric mappings are unchanged.
 * Preparation may allocate. Failure leaves source and output unchanged.
 */
MetadataCaptureTranslationResult
translate_xmp_capture_settings_metadata(
    const MetaStore& source,
    const MetadataCaptureSettingsTranslationOptions& options,
    MetaStore* out_store);

/// Experimental exact capture-rational writeback contract.
inline constexpr uint32_t kMetadataCaptureRationalTranslationContractVersion
    = 1U;
inline constexpr uint32_t kMetadataCaptureRationalTranslationMaxAddedEntries
    = 4U;
inline constexpr uint64_t kMetadataCaptureRationalTranslationMaxTotalTextBytes
    = 512U;

struct MetadataCaptureRationalTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    /// exif:SubjectDistance -> ExifIFD 0x9206 RATIONAL, meters.
    bool subject_distance_to_exif = true;
    /// exif:DigitalZoomRatio -> ExifIFD 0xa404 RATIONAL.
    bool digital_zoom_ratio_to_exif = true;
    /// exif:ExposureIndex -> ExifIFD 0xa215 RATIONAL.
    bool exposure_index_to_exif = true;
    /// exif:FlashEnergy -> ExifIFD 0xa20b RATIONAL, BCPS.
    bool flash_energy_to_exif = true;
    uint32_t max_added_entries
        = kMetadataCaptureRationalTranslationMaxAddedEntries;
    uint32_t max_operations = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataCaptureRationalTranslationMaxTotalTextBytes;
};

/**
 * \brief Translate four exact unsigned capture rationals atomically.
 *
 * Accepts scalar integers, unsigned rationals, and exact decimal/scientific or
 * fraction text. SubjectDistance zero means unknown; a wire numerator of
 * UINT32_MAX means infinity. Exact Unknown/Infinity labels are also accepted.
 * Decimal/scientific finite distances that would encode the infinity sentinel
 * fail. DigitalZoomRatio zero means unused; ExposureIndex must be positive;
 * FlashEnergy may be zero. Zero denominators and floating-point values fail.
 * No units, APEX values, flash state, or EXIF versions are inferred or converted.
 * Older TIFF/EP aliases are not targets. Preparation may allocate; failure leaves
 * source and output unchanged, including aliased calls.
 */
MetadataCaptureTranslationResult
translate_xmp_capture_rational_metadata(
    const MetaStore& source,
    const MetadataCaptureRationalTranslationOptions& options,
    MetaStore* out_store);

/// Experimental complete Flash-bitfield writeback contract.
inline constexpr uint32_t kMetadataFlashTranslationContractVersion     = 1U;
inline constexpr uint32_t kMetadataFlashTranslationMaxAddedEntries     = 1U;
inline constexpr uint32_t kMetadataFlashTranslationMaxSourceProperties = 16U;
inline constexpr uint64_t kMetadataFlashTranslationMaxTotalTextBytes   = 640U;

struct MetadataFlashTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    uint32_t max_added_entries = kMetadataFlashTranslationMaxAddedEntries;
    uint32_t max_source_properties
        = kMetadataFlashTranslationMaxSourceProperties;
    uint32_t max_operations = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes = kMetadataFlashTranslationMaxTotalTextBytes;
};

/**
 * \brief Write one complete ExifIFD Flash SHORT without inferring missing bits.
 *
 * Accepts either scalar exif:Flash (integer or exact OpenMeta label), or all five
 * structured Fired, Function, Mode, RedEyeMode, and Return children. Function
 * true means no flash function. One dirty child selects the complete active group
 * in DirtyOnly mode. A dirty scalar tombstone or five dirty child tombstones
 * removes the native tag; partial groups/deletion and competing shapes fail.
 * Reserved high bits and Return code 1 fail. No physical-state consistency or
 * exposure/FlashEnergy inference is performed. All conflicts and resource checks
 * precede one atomic transaction. Preparation may allocate.
 */
MetadataCaptureTranslationResult
translate_xmp_flash_metadata(const MetaStore& source,
                             const MetadataFlashTranslationOptions& options,
                             MetaStore* out_store);

/// Experimental unambiguous LightSource writeback contract.
inline constexpr uint32_t kMetadataLightSourceTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataLightSourceTranslationMaxAddedEntries = 1U;
inline constexpr uint64_t kMetadataLightSourceTranslationMaxTotalTextBytes
    = 128U;

struct MetadataLightSourceTranslationOptions final {
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    uint32_t max_added_entries = kMetadataLightSourceTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataLightSourceTranslationMaxTotalTextBytes;
};

/**
 * \brief Translate one exact exif:LightSource to ExifIFD 0x9208 SHORT.
 *
 * Accepts scalar integers or unsigned decimal integer text for codes 0..4,
 * 9..34, and 255. Exact unique OpenMeta labels and the explicit aliases Tungsten
 * and Cloudy weather are accepted. Daylight is ambiguous between codes 1 and 25
 * and fails even if native metadata could supply a preferred interpretation.
 * No white-balance, Flash, illuminant, color-temperature or version inference is
 * performed. Missing sources retain native values; dirty tombstones remove them
 * under ReplaceExisting. All changes are atomic and preparation may allocate.
 */
MetadataCaptureTranslationResult
translate_xmp_light_source_metadata(
    const MetaStore& source,
    const MetadataLightSourceTranslationOptions& options, MetaStore* out_store);

/// Experimental complete sensitivity-group writeback contract.
inline constexpr uint32_t kMetadataSensitivityTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataSensitivityTranslationMaxAddedEntries = 7U;
inline constexpr uint64_t kMetadataSensitivityTranslationMaxTotalTextBytes
    = 896U;

struct MetadataSensitivityTranslationOptions final {
    /// DirtyOnly selects the group when any recognized member is Dirty;
    /// active clean companions participate once the group is selected.
    MetadataCaptureTranslationSourceMode source_mode
        = MetadataCaptureTranslationSourceMode::DirtyOnly;
    MetadataCaptureTranslationConflictPolicy conflict_policy
        = MetadataCaptureTranslationConflictPolicy::FailOnConflict;
    uint32_t max_added_entries = kMetadataSensitivityTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataCaptureTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataCaptureTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataSensitivityTranslationMaxTotalTextBytes;
};

/**
 * \brief Translate one complete seven-tag EXIF sensitivity group.
 *
 * Accepts exact exifEX properties PhotographicSensitivity, SensitivityType,
 * StandardOutputSensitivity, RecommendedExposureIndex, ISOSpeed,
 * ISOSpeedLatitudeyyy and ISOSpeedLatitudezzz. Legacy OpenMeta exif:ISO and
 * companion names, and single-member exif:ISOSpeedRatings, are accepted aliases.
 * Duplicate aliases are ambiguous. Values are unsigned integer scalars or
 * decimal integer text. PhotographicSensitivity is 1..65535, SensitivityType
 * is 0..7, and optional LONG companions are 1..UINT32_MAX.
 *
 * Active groups require PhotographicSensitivity and SensitivityType. Supplied
 * parameters selected by the type must agree, with values >=65535 represented
 * by 65535 in the SHORT. Latitude values require both latitude tags and ISOSpeed.
 * Missing optional sources mean absent target members. A base-property dirty
 * tombstone with no active members removes the whole group under ReplaceExisting.
 * PreserveExisting preserves any existing group; FailOnConflict requires the
 * complete group to be absent or exactly equivalent. No type, high-range value,
 * exposure setting or EXIF version is inferred. All seven tags share one result
 * group and one transaction; cold preparation may allocate.
 */
MetadataCaptureTranslationResult
translate_xmp_sensitivity_metadata(
    const MetaStore& source,
    const MetadataSensitivityTranslationOptions& options, MetaStore* out_store);

/// Experimental target-bound image-geometry translation contract version.
inline constexpr uint32_t kMetadataGeometryTranslationContractVersion = 1U;

inline constexpr uint32_t kMetadataGeometryTranslationMaxAddedEntries = 5U;
inline constexpr uint32_t kMetadataGeometryTranslationMaxOperations   = 1024U;
inline constexpr uint32_t kMetadataGeometryTranslationMaxTextBytesPerProperty
    = 32U;
inline constexpr uint64_t kMetadataGeometryTranslationMaxTotalTextBytes = 160U;

/// Which exact XMP properties are eligible as reverse geometry sources.
enum class MetadataGeometryTranslationSourceMode : uint8_t {
    /// Translate only a group with at least one Dirty member.
    DirtyOnly,
    /// Translate active clean or dirty entries; tombstones still require Dirty.
    All,
};

/// How an existing native EXIF geometry group is reconciled.
enum class MetadataGeometryTranslationConflictPolicy : uint8_t {
    PreserveExisting,
    FailOnConflict,
    ReplaceExisting,
};

/// Exact image-geometry source mapping associated with a result or failure.
enum class MetadataGeometryTranslationMapping : uint8_t {
    None,
    XmpOrientation,
    XmpDimensions,
};

/// Caller-selected bounded reverse image-geometry mappings.
struct MetadataGeometryTranslationOptions final {
    MetadataGeometryTranslationSourceMode source_mode
        = MetadataGeometryTranslationSourceMode::DirtyOnly;
    MetadataGeometryTranslationConflictPolicy conflict_policy
        = MetadataGeometryTranslationConflictPolicy::FailOnConflict;

    /// tiff:Orientation -> IFD0 Orientation SHORT.
    bool orientation_to_exif = true;
    /// XMP stored-raster dimensions -> canonical TIFF/EXIF LONG dimensions.
    bool dimensions_to_exif = true;

    uint32_t max_added_entries = kMetadataGeometryTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataGeometryTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataGeometryTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataGeometryTranslationMaxTotalTextBytes;
};

enum class MetadataGeometryTranslationStatus : uint8_t {
    Ok,
    NullOutput,
    SourceNotFinalized,
    InvalidOptions,
    InvalidTargetImageSpec,
    TargetImageSpecRequired,
    TargetImageSpecMismatch,
    AmbiguousSource,
    IncompleteSourceGroup,
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

/// Transactional result details for one reverse image-geometry translation.
struct MetadataGeometryTranslationResult final {
    MetadataGeometryTranslationStatus status
        = MetadataGeometryTranslationStatus::Ok;
    MetadataGeometryTranslationMapping failed_mapping
        = MetadataGeometryTranslationMapping::None;
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
 * \brief Translate exact XMP image geometry into target-bound native EXIF.
 *
 * Active orientation and dimension sources must exactly match the corresponding
 * host-supplied target facts. Width and height are stored-raster dimensions;
 * they are never swapped according to display orientation. The dimension group
 * emits canonical IFD0 ImageWidth/ImageLength and ExifIFD
 * PixelXDimension/PixelYDimension LONG values. Orientation emits one IFD0
 * Orientation SHORT.
 *
 * Standard and OpenMeta-portable dimension aliases may coexist only when they
 * agree. Duplicate exact properties, incomplete active/deleted dimension pairs,
 * missing target facts, and source/target mismatches fail transactionally.
 */
MetadataGeometryTranslationResult
translate_xmp_image_geometry(const MetaStore& source,
                             const TransferTargetImageSpec& target_image_spec,
                             const MetadataGeometryTranslationOptions& options,
                             MetaStore* out_store);

const char*
metadata_geometry_translation_status_name(
    MetadataGeometryTranslationStatus status) noexcept;

const char*
metadata_geometry_translation_mapping_name(
    MetadataGeometryTranslationMapping mapping) noexcept;

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

/// Exact descriptive, location, or editorial source mapping for a result.
enum class MetadataDescriptiveTranslationMapping : uint8_t {
    None,
    DcTitle,
    DcDescription,
    DcCreator,
    DcSubject,
    DcRights,
    PhotoshopCredit,
    PhotoshopSource,
    PhotoshopCity,
    IptcLocation,
    PhotoshopState,
    PhotoshopCountry,
    IptcCountryCode,
    PhotoshopHeadline,
    PhotoshopInstructions,
    PhotoshopTransmissionReference,
    PhotoshopAuthorsPosition,
    PhotoshopCaptionWriter,
    PhotoshopCategory,
    PhotoshopSupplementalCategories,
    PhotoshopUrgency,
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
    AmbiguousLocation,
    LocationNotFound,
    UnsupportedSourceShape,
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

/// Experimental reverse IPTC Core location translation contract version.
inline constexpr uint32_t kMetadataLocationTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataLocationTranslationMaxAddedEntries = 6U;

/// Independent flat location mappings using the descriptive IPTC policies.
struct MetadataLocationTranslationOptions final {
    MetadataDescriptiveTranslationSourceMode source_mode
        = MetadataDescriptiveTranslationSourceMode::DirtyOnly;
    MetadataDescriptiveTranslationConflictPolicy conflict_policy
        = MetadataDescriptiveTranslationConflictPolicy::FailOnConflict;

    bool city_to_iptc         = true;
    bool sublocation_to_iptc  = true;
    bool state_to_iptc        = true;
    bool country_to_iptc      = true;
    bool country_code_to_iptc = true;

    uint32_t max_source_properties
        = kMetadataDescriptiveTranslationMaxSourceProperties;
    uint32_t max_added_entries = kMetadataLocationTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataDescriptiveTranslationMaxOperations;
    uint64_t max_total_text_bytes
        = kMetadataDescriptiveTranslationMaxTotalTextBytes;
};

/**
 * \brief Translate flat IPTC Core XMP location properties into native IPTC-IIM.
 *
 * Exact mappings are photoshop:City, Iptc4xmpCore:Location, photoshop:State,
 * photoshop:Country, and Iptc4xmpCore:CountryCode. Country codes require two
 * or three uppercase ASCII letters; code membership and country-name agreement
 * are caller responsibilities. Text byte limits and UTF-8 charset safety use
 * the same transaction as descriptive translation. Duplicate active singleton
 * sources are ambiguous. Structured locations and GPS are not aliases.
 *
 * Policies, status, mapping diagnostics, and counters use the descriptive
 * translation types. At most five datasets and one charset entry are added.
 * The immutable source and output are unchanged on failure.
 */
MetadataDescriptiveTranslationResult
translate_xmp_location_metadata(
    const MetaStore& source, const MetadataLocationTranslationOptions& options,
    MetaStore* out_store);

/// Experimental structured location to flat XMP and IPTC reconciliation.
inline constexpr uint32_t kMetadataStructuredLocationTranslationContractVersion
    = 1U;
inline constexpr uint32_t kMetadataStructuredLocationTranslationMaxAddedEntries
    = 11U;

enum class MetadataStructuredLocationKind : uint8_t {
    Shown,
    Created,
    Unspecified
};

struct MetadataStructuredLocationTranslationOptions final {
    MetadataStructuredLocationKind location_kind
        = MetadataStructuredLocationKind::Shown;
    /// Zero requires a single record; positive values select an exact XMP index.
    uint32_t location_index = 0U;
    MetadataDescriptiveTranslationSourceMode source_mode
        = MetadataDescriptiveTranslationSourceMode::DirtyOnly;
    MetadataDescriptiveTranslationConflictPolicy conflict_policy
        = MetadataDescriptiveTranslationConflictPolicy::FailOnConflict;
    bool city         = true;
    bool sublocation  = true;
    bool state        = true;
    bool country      = true;
    bool country_code = true;
    uint32_t max_source_properties
        = kMetadataDescriptiveTranslationMaxSourceProperties;
    uint32_t max_added_entries
        = kMetadataStructuredLocationTranslationMaxAddedEntries;
    uint32_t max_operations = kMetadataDescriptiveTranslationMaxOperations;
    uint64_t max_total_text_bytes
        = kMetadataDescriptiveTranslationMaxTotalTextBytes;
};

/**
 * \brief Reconcile five text fields from one explicit structured location into
 * flat legacy XMP and native IPTC-IIM in a single atomic transaction.
 *
 * Exact Iptc4xmpExt LocationShown[n] or LocationCreated[n] records are selected.
 * The existing scalar LocationCreated resource form is accepted as index one.
 * Created and Shown never fall back to each other. Multiple records require
 * an explicit index. GPS, nested Address fields, names, IDs, and WorldRegion
 * are retained but not projected. No record is merged with another record.
 *
 * City, Sublocation, ProvinceState, CountryName, and CountryCode use the flat
 * location text limits and charset policy. Each flat XMP/native IPTC pair is
 * one conflict group. PreserveExisting retains both if either exists; missing
 * structured fields are untouched. Dirty field tombstones remove both under
 * ReplaceExisting. Structure/array tombstones are not field-removal requests.
 * Counters include both destinations and any UTF-8 marker. Failure leaves source
 * and output unchanged; preparation may allocate. Existing APIs are unchanged.
 */
MetadataDescriptiveTranslationResult
translate_xmp_structured_location_metadata(
    const MetaStore& source,
    const MetadataStructuredLocationTranslationOptions& options,
    MetaStore* out_store);

/// Experimental flat-XMP-to-structured-location construction contract.
inline constexpr uint32_t kMetadataLocationCreationTranslationContractVersion
    = 1U;
inline constexpr uint32_t kMetadataLocationCreationTranslationMaxAddedEntries
    = 5U;
inline constexpr uint32_t kMetadataLocationCreationTranslationMaxIndex = 1024U;

struct MetadataLocationCreationTranslationOptions final {
    /// Both kind and index require explicit caller selection.
    MetadataStructuredLocationKind location_kind
        = MetadataStructuredLocationKind::Unspecified;
    uint32_t location_index = 0U;
    MetadataDescriptiveTranslationSourceMode source_mode
        = MetadataDescriptiveTranslationSourceMode::DirtyOnly;
    MetadataDescriptiveTranslationConflictPolicy conflict_policy
        = MetadataDescriptiveTranslationConflictPolicy::FailOnConflict;
    bool city         = true;
    bool sublocation  = true;
    bool state        = true;
    bool country      = true;
    bool country_code = true;
    uint32_t max_source_properties
        = kMetadataDescriptiveTranslationMaxSourceProperties;
    uint32_t max_added_entries
        = kMetadataLocationCreationTranslationMaxAddedEntries;
    uint32_t max_operations = kMetadataDescriptiveTranslationMaxOperations;
    uint64_t max_total_text_bytes
        = kMetadataDescriptiveTranslationMaxTotalTextBytes;
};

/**
 * \brief Copy five exact flat legacy XMP location fields into one explicitly
 * selected IPTC Extension LocationShown or LocationCreated record.
 *
 * Uses the flat location text limits and exact country-code contract. Native
 * IPTC is not an implicit source. Missing fields retain destinations; dirty
 * leaf tombstones remove selected leaves under ReplaceExisting. Unrelated
 * fields and records remain untouched. No geographic or Created/Shown inference.
 *
 * Created permits index one only. Shown permits an existing record or append
 * at the next dense index, bounded by 1024. Sparse/mixed or opaque container
 * shapes fail. Removing an entire non-last record fails to prevent reindexing.
 * Existing scalar Created resources retain their shape; new records use the
 * indexed Bag form. All selected fields commit atomically; preparation may allocate.
 */
MetadataDescriptiveTranslationResult
translate_xmp_location_to_structured_metadata(
    const MetaStore& source,
    const MetadataLocationCreationTranslationOptions& options,
    MetaStore* out_store);


/// Experimental reverse IPTC editorial translation contract version.
inline constexpr uint32_t kMetadataEditorialTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataEditorialTranslationMaxAddedEntries = 4U;

/// Independent editorial mappings using the descriptive IPTC policies.
struct MetadataEditorialTranslationOptions final {
    MetadataDescriptiveTranslationSourceMode source_mode
        = MetadataDescriptiveTranslationSourceMode::DirtyOnly;
    MetadataDescriptiveTranslationConflictPolicy conflict_policy
        = MetadataDescriptiveTranslationConflictPolicy::FailOnConflict;

    bool headline_to_iptc               = true;
    bool instructions_to_iptc           = true;
    bool transmission_reference_to_iptc = true;

    uint32_t max_source_properties
        = kMetadataDescriptiveTranslationMaxSourceProperties;
    uint32_t max_added_entries = kMetadataEditorialTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataDescriptiveTranslationMaxOperations;
    uint64_t max_total_text_bytes
        = kMetadataDescriptiveTranslationMaxTotalTextBytes;
};

/**
 * \brief Translate exact Photoshop XMP editorial properties into IPTC-IIM.
 *
 * Headline maps to 2:105 (256 bytes), Instructions to 2:40 (256 bytes), and
 * TransmissionReference to 2:103 (32 bytes). All are singleton text properties.
 * Limits count UTF-8 bytes and never truncate. Title, description, rights, and
 * other identifiers are not aliases for these properties.
 *
 * Policies, status, diagnostics, and counters use the descriptive translation
 * types. Duplicate active sources are ambiguous. Dirty deletions remove native
 * groups only with ReplaceExisting. Charset safety and resource limits use the
 * same transaction as descriptive translation. At most three datasets and one
 * charset entry are added. The source and output are unchanged on failure.
 */
MetadataDescriptiveTranslationResult
translate_xmp_editorial_metadata(
    const MetaStore& source, const MetadataEditorialTranslationOptions& options,
    MetaStore* out_store);

/// Experimental combined IPTC text/priority translation contract version.
inline constexpr uint32_t kMetadataIptcTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataIptcTranslationMaxAddedEntries = 1025U;

/// All supported IPTC text/priority mappings in one transaction.
struct MetadataIptcTranslationOptions final {
    MetadataDescriptiveTranslationSourceMode source_mode
        = MetadataDescriptiveTranslationSourceMode::DirtyOnly;
    MetadataDescriptiveTranslationConflictPolicy conflict_policy
        = MetadataDescriptiveTranslationConflictPolicy::FailOnConflict;

    bool title_to_iptc_object_name       = true;
    bool description_to_iptc_caption     = true;
    bool creators_to_iptc_bylines        = true;
    bool keywords_to_iptc_keywords       = true;
    bool copyright_to_iptc_copyright     = true;
    bool credit_to_iptc_credit           = true;
    bool source_to_iptc_source           = true;
    bool city_to_iptc                    = true;
    bool sublocation_to_iptc             = true;
    bool state_to_iptc                   = true;
    bool country_to_iptc                 = true;
    bool country_code_to_iptc            = true;
    bool headline_to_iptc                = true;
    bool instructions_to_iptc            = true;
    bool transmission_reference_to_iptc  = true;
    bool authors_position_to_iptc        = true;
    bool caption_writer_to_iptc          = true;
    bool category_to_iptc                = true;
    bool supplemental_categories_to_iptc = true;
    bool urgency_to_iptc                 = true;

    uint32_t max_source_properties
        = kMetadataDescriptiveTranslationMaxSourceProperties;
    uint32_t max_added_entries = kMetadataIptcTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataDescriptiveTranslationMaxOperations;
    uint64_t max_total_text_bytes
        = kMetadataDescriptiveTranslationMaxTotalTextBytes;
};

/**
 * \brief Translate 20 XMP text/priority groups into IPTC-IIM atomically.
 *
 * Includes the descriptive, location, and editorial mappings, plus Photoshop
 * AuthorsPosition (2:85, 32 bytes), CaptionWriter (2:122, 32 bytes), Category
 * (2:15, one to three ASCII letters), indexed SupplementalCategories (2:20,
 * 32 bytes each), and Urgency (2:10, one digit from 1 to 8). Urgency also accepts
 * a signed or unsigned integer scalar in that range. Repeated values retain
 * numeric XMP index order and duplicates at distinct indexes.
 *
 * Source selection, conflicts, dirty removal, provenance, byte/resource limits,
 * and charset safety share one transaction across all selected groups. Date
 * fields use translate_xmp_creation_dates separately. The caller associates
 * AuthorsPosition with the first creator; this operation does not infer that
 * relationship or create a missing creator. Existing subgroup APIs are unchanged.
 */
MetadataDescriptiveTranslationResult
translate_xmp_iptc_metadata(const MetaStore& source,
                            const MetadataIptcTranslationOptions& options,
                            MetaStore* out_store);

/// Experimental primary GPS position/altitude writeback contract.
inline constexpr uint32_t kMetadataGpsTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataGpsTranslationMaxAddedEntries = 7U;
inline constexpr uint32_t kMetadataGpsTranslationMaxOperations   = 1024U;
inline constexpr uint32_t kMetadataGpsTranslationMaxTextBytesPerProperty = 128U;
inline constexpr uint64_t kMetadataGpsTranslationMaxTotalTextBytes       = 512U;

enum class MetadataGpsTranslationSourceMode : uint8_t { DirtyOnly, All };
enum class MetadataGpsTranslationConflictPolicy : uint8_t {
    PreserveExisting,
    FailOnConflict,
    ReplaceExisting,
};
enum class MetadataGpsTranslationMapping : uint8_t {
    None,
    ExifGpsLatitude,
    ExifGpsLongitude,
    ExifGpsAltitude,
    GpsVersion,
    ExifGpsTimeStamp,
    ExifGpsSpeed,
    ExifGpsTrack,
    ExifGpsImgDirection,
    ExifGpsDestLatitude,
    ExifGpsDestLongitude,
    ExifGpsDestBearing,
    ExifGpsDestDistance,
    ExifGpsStatus,
    ExifGpsMeasureMode,
    ExifGpsDop,
    ExifGpsDifferential,
    ExifGpsHPositioningError,
    ExifGpsSatellites,
    ExifGpsMapDatum,
    ExifGpsProcessingMethod,
    ExifGpsAreaInformation,
};
enum class MetadataGpsTranslationStatus : uint8_t {
    Ok,
    NullOutput,
    SourceNotFinalized,
    InvalidOptions,
    AmbiguousSource,
    IncompleteSource,
    InvalidSourceValue,
    ValueOutOfRange,
    UnsupportedPrecision,
    UnsupportedGpsVersion,
    ValueTooLong,
    SourceLimitExceeded,
    NativeConflict,
    EntryLimitExceeded,
    OperationLimitExceeded,
    InternalError,
};

struct MetadataGpsTranslationOptions final {
    MetadataGpsTranslationSourceMode source_mode
        = MetadataGpsTranslationSourceMode::DirtyOnly;
    MetadataGpsTranslationConflictPolicy conflict_policy
        = MetadataGpsTranslationConflictPolicy::FailOnConflict;
    bool latitude_to_exif      = true;
    bool longitude_to_exif     = true;
    bool altitude_to_exif      = true;
    uint32_t max_added_entries = kMetadataGpsTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataGpsTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataGpsTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes = kMetadataGpsTranslationMaxTotalTextBytes;
};

struct MetadataGpsTranslationResult final {
    MetadataGpsTranslationStatus status = MetadataGpsTranslationStatus::Ok;
    MetadataGpsTranslationMapping failed_mapping
        = MetadataGpsTranslationMapping::None;
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
 * \brief Atomically translate primary exif:GPSLatitude, GPSLongitude, and
 * GPSAltitude/GPSAltitudeRef XMP properties into native gpsifd entries.
 *
 * Coordinates accept unsigned degrees plus decimal minutes or integer minutes
 * plus decimal seconds, separated by commas and followed by uppercase N/S or
 * E/W. Output is exact normalized DMS RATIONAL[3] plus ASCII reference.
 * Altitude is nonnegative exact text/integer/rational with a required XMP
 * sea-level reference 0 or 1. GPS 2.4 uses native sea-level codes 2 or 3;
 * GPS 2.0 through 2.3 use 0 or 1. No ellipsoid/datum conversion is performed.
 *
 * One dirty altitude member selects the complete active pair. Removing the
 * altitude group requires both members to be dirty tombstones. Native groups
 * reconcile as complete pairs. GPSVersionID is retained, or 2.3.0.0 is added
 * when selected active output needs it. Removing the last GPS value also
 * removes its version tag; unrelated GPS fields prevent that cleanup.
 *
 * Failure leaves source and output unchanged. Preparation may allocate.
 */
MetadataGpsTranslationResult
translate_xmp_gps_metadata(const MetaStore& source,
                           const MetadataGpsTranslationOptions& options,
                           MetaStore* out_store);

inline constexpr uint32_t kMetadataGpsNavigationTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataGpsNavigationTranslationMaxAddedEntries = 9U;
inline constexpr uint64_t kMetadataGpsNavigationTranslationMaxTotalTextBytes
    = 896U;

struct MetadataGpsNavigationTranslationOptions final {
    MetadataGpsTranslationSourceMode source_mode
        = MetadataGpsTranslationSourceMode::DirtyOnly;
    MetadataGpsTranslationConflictPolicy conflict_policy
        = MetadataGpsTranslationConflictPolicy::FailOnConflict;
    bool timestamp_to_exif       = true;
    bool speed_to_exif           = true;
    bool track_to_exif           = true;
    bool image_direction_to_exif = true;
    uint32_t max_added_entries
        = kMetadataGpsNavigationTranslationMaxAddedEntries;
    uint32_t max_operations = kMetadataGpsTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataGpsTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataGpsNavigationTranslationMaxTotalTextBytes;
};

/**
 * \brief Atomically translate GPS time, speed, track, and image direction.
 *
 * GPSTimeStamp requires a complete ISO date/time with an explicit timezone.
 * Numeric offsets are normalized to UTC; fractional seconds stay exact.
 * Output is GPSDateStamp plus GPSTimeStamp RATIONAL[3]. Leap seconds and
 * partial dates/times are unsupported. Date output requires GPS 2.2 through
 * 2.4; a missing native version defaults to 2.3.0.0.
 *
 * Speed/track/image direction require their complete XMP value/reference pair.
 * Values use the primary GPS exact unsigned rational syntax. Angles are in
 * [0, 359.99]. Canonical K/M/N and T/M references, plus the existing portable
 * aliases km/h, mph, knots, True North, and Magnetic North, are accepted.
 * Units and north references are retained without conversion or inference.
 *
 * Source selection, paired conflicts/removal, version cleanup, provenance,
 * and failure atomicity follow the primary GPS contract. That API's options
 * and mapping set are unchanged. Preparation may allocate.
 */
MetadataGpsTranslationResult
translate_xmp_gps_navigation_metadata(
    const MetaStore& source,
    const MetadataGpsNavigationTranslationOptions& options,
    MetaStore* out_store);

inline constexpr uint32_t kMetadataGpsDestinationTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataGpsDestinationTranslationMaxAddedEntries = 9U;
inline constexpr uint64_t kMetadataGpsDestinationTranslationMaxTotalTextBytes
    = 768U;

struct MetadataGpsDestinationTranslationOptions final {
    MetadataGpsTranslationSourceMode source_mode
        = MetadataGpsTranslationSourceMode::DirtyOnly;
    MetadataGpsTranslationConflictPolicy conflict_policy
        = MetadataGpsTranslationConflictPolicy::FailOnConflict;
    bool latitude_to_exif  = true;
    bool longitude_to_exif = true;
    bool bearing_to_exif   = true;
    bool distance_to_exif  = true;
    uint32_t max_added_entries
        = kMetadataGpsDestinationTranslationMaxAddedEntries;
    uint32_t max_operations = kMetadataGpsTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataGpsTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataGpsDestinationTranslationMaxTotalTextBytes;
};

/**
 * \brief Atomically translate destination GPS coordinates, bearing, and distance.
 *
 * GPSDestLatitude/Longitude use the primary GPS exact coordinate syntax.
 * GPSDestBearing and GPSDestDistance each require their complete XMP
 * reference/value pair. Bearing is in [0, 359.99], with T/M or the portable
 * True North/Magnetic North aliases. Distance is an exact unsigned rational,
 * with K/M/N units (kilometers, miles, nautical miles). Kilometers, Miles,
 * and Nautical miles are accepted aliases. Historical portable Knots also
 * means N distance; it never requests a speed or unit conversion.
 *
 * No position, bearing, distance, datum, or north reference is inferred or
 * converted. Source selection, paired conflicts/removal, version cleanup,
 * provenance, limits, and failure atomicity follow the primary GPS contract.
 * Existing primary and navigation APIs are unchanged. Preparation may allocate.
 */
MetadataGpsTranslationResult
translate_xmp_gps_destination_metadata(
    const MetaStore& source,
    const MetadataGpsDestinationTranslationOptions& options,
    MetaStore* out_store);

inline constexpr uint32_t kMetadataGpsQualityTranslationContractVersion = 1U;
inline constexpr uint32_t kMetadataGpsQualityTranslationMaxAddedEntries = 6U;
inline constexpr uint64_t kMetadataGpsQualityTranslationMaxTotalTextBytes = 640U;

struct MetadataGpsQualityTranslationOptions final {
    MetadataGpsTranslationSourceMode source_mode
        = MetadataGpsTranslationSourceMode::DirtyOnly;
    MetadataGpsTranslationConflictPolicy conflict_policy
        = MetadataGpsTranslationConflictPolicy::FailOnConflict;
    bool status_to_exif           = true;
    bool measure_mode_to_exif     = true;
    bool dop_to_exif              = true;
    bool differential_to_exif     = true;
    bool horizontal_error_to_exif = true;
    uint32_t max_added_entries = kMetadataGpsQualityTranslationMaxAddedEntries;
    uint32_t max_operations    = kMetadataGpsTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataGpsTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes
        = kMetadataGpsQualityTranslationMaxTotalTextBytes;
};

/**
 * \brief Atomically write receiver status, mode, DOP, correction, and accuracy.
 *
 * GPSStatus accepts A/V or Measurement Active/Measurement Void. Measure mode
 * accepts integer 2/3 or their exact text forms. Differential accepts integer
 * 0/1, their text forms, or No Correction/Differential Corrected. DOP and
 * horizontal error use exact nonnegative rational syntax; horizontal error is
 * in meters. Differential requires GPS 2.2 through 2.4; horizontal error
 * requires GPS 2.3 through 2.4. Versions are never inferred from source XMP
 * or upgraded; missing native versions default to 2.3.0.0.
 *
 * Each source is independent. One dirty tombstone removes its native field.
 * Selection, conflict policy, version cleanup, provenance, limits, and failure
 * atomicity follow the primary GPS contract. Preparation may allocate.
 */
MetadataGpsTranslationResult
translate_xmp_gps_quality_metadata(
    const MetaStore& source,
    const MetadataGpsQualityTranslationOptions& options, MetaStore* out_store);

inline constexpr uint32_t kMetadataGpsTextTranslationContractVersion   = 1U;
inline constexpr uint32_t kMetadataGpsTextTranslationMaxAddedEntries   = 5U;
inline constexpr uint64_t kMetadataGpsTextTranslationMaxTotalTextBytes = 16384U;

inline constexpr uint32_t kMetadataGpsTextTranslationMaxTextBytesPerProperty
    = 4096U;

struct MetadataGpsTextTranslationOptions final {
    MetadataGpsTranslationSourceMode source_mode
        = MetadataGpsTranslationSourceMode::DirtyOnly;
    MetadataGpsTranslationConflictPolicy conflict_policy
        = MetadataGpsTranslationConflictPolicy::FailOnConflict;
    bool satellites_to_exif        = true;
    bool map_datum_to_exif         = true;
    bool processing_method_to_exif = true;
    bool area_information_to_exif  = true;
    uint32_t max_added_entries     = kMetadataGpsTextTranslationMaxAddedEntries;
    uint32_t max_operations        = kMetadataGpsTranslationMaxOperations;
    uint32_t max_text_bytes_per_property
        = kMetadataGpsTextTranslationMaxTextBytesPerProperty;
    uint64_t max_total_text_bytes = kMetadataGpsTextTranslationMaxTotalTextBytes;
};

/**
 * \brief Atomically write satellite, datum, processing-method, and area text.
 *
 * Satellites/datum require ASCII. Method/area accept valid UTF-8 and write
 * UNDEFINED with ASCII or UNICODE prefixes; Unicode output uses UTF-16LE BOM.
 * Empty text is a value. NUL/control characters and malformed UTF-8 fail.
 * Native equivalence recognizes ASCII and BOM-marked UTF-16, optionally with
 * one terminator. JIS, unknown prefixes, and BOM-less Unicode are not guessed.
 * Method/area require GPS 2.2 through 2.4; missing versions default to 2.3.0.0.
 * No datum conversion or processing/receiver-state inference is performed.
 * Singleton selection, removal, conflict, version, and atomicity follow the
 * quality GPS contract. Preparation may allocate.
 */
MetadataGpsTranslationResult
translate_xmp_gps_text_metadata(const MetaStore& source,
                                const MetadataGpsTextTranslationOptions& options,
                                MetaStore* out_store);

const char*
metadata_gps_translation_status_name(
    MetadataGpsTranslationStatus status) noexcept;
const char*
metadata_gps_translation_mapping_name(
    MetadataGpsTranslationMapping mapping) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
