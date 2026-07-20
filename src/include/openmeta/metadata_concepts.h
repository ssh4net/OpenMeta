// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/meta_store.h"
#include "openmeta/metadata_query.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * \file metadata_concepts.h
 * \brief Experimental cross-family metadata concept resolution.
 */

namespace openmeta {

enum class MetadataConceptKind : uint8_t {
    Orientation,
    DateTime,
    ColorProfile,
    Gps,
    Geometry,
    LensCorrection,
    RawProcessing,
    Exposure,
    ContainerGraph,
    Descriptive,
};

enum class MetadataConceptSourceFamily : uint8_t {
    Unknown,
    Exif,
    Xmp,
    Iptc,
    Icc,
    PngText,
    InterpretationRecord,
};

enum class MetadataConceptRole : uint8_t {
    Primary,
    Orientation,
    Created,
    Digitized,
    Modified,
    MetadataDate,
    DateCreated,
    ColorSpace,
    IccProfile,
    ColorMatrix,
    WhiteBalance,
    Latitude,
    Longitude,
    Altitude,
    Timestamp,
    Crop,
    ActiveArea,
    Border,
    SensorGeometry,
    LensCorrection,
    BlackLevel,
    WhiteLevel,
    Linearization,
    CfaLayout,
    RawStorage,
    SourceProcessing,
    ComputationalProcessing,
    ThermalProcessing,
    StitchProcessing,
    ExposureTime,
    Aperture,
    IsoSensitivity,
    ExposureBias,
    ExposureProgram,
    Gain,
    RawExposureAdjustment,
    SourceColorTransform,
    RawValueCurve,
    RawLinearityLimit,
    RawCalibrationCurve,
    RawCurveControlPoints,
    ContentBoundMetadata,
    MultiImageScene,
    DerivedImageConstruction,
    TiledImageConfiguration,
    DestinationLatitude,
    DestinationLongitude,
    LocationShownLatitude,
    LocationShownLongitude,
    LocationShownAltitude,
    LocationCreatedLatitude,
    LocationCreatedLongitude,
    LocationCreatedAltitude,
    Title,
    Headline,
    Description,
    Creator,
    Keywords,
    LocationName,
    Sublocation,
    City,
    ProvinceState,
    CountryName,
    CountryCode,
    WorldRegion,
    LocationIdentifier,
    CopyrightNotice,
    CopyrightStatus,
    RightsUsageTerms,
    RightsWebStatement,
    RightsCertificate,
    RightsMarked,
    RightsHolderName,
    RightsHolderIdentifier,
    LicenseIdentifier,
    LicenseTermsUrl,
    LicensorName,
    LicensorIdentifier,
    CreditLine,
    CreditLineRequired,
    Source,
    DigitalSourceType,
    Name,
    Identifier,
    Address,
    PostalCode,
    Email,
    Telephone,
    Url,
    Characteristic,
    Gtin,
    InventoryNumber,
    StylePeriod,
    CreatorIdentifier,
    Age,
    ContentDescription,
    ContributionDescription,
    PhysicalDescription,
    RightsExpression,
    RightsExpressionEncoding,
    RightsExpressionLanguage,
    LicenseStartDate,
    LicenseEndDate,
    MediaConstraint,
    RegionConstraint,
    ProductOrServiceConstraint,
    ImageFileConstraint,
    ImageAlterationConstraint,
    OtherLicenseRequirement,
    OtherCondition,
    LicenseeTransactionIdentifier,
    LicensorTransactionIdentifier,
    LicenseeProjectReference,
    LicenseTransactionDate,
    ReleaseStatus,
    ReleaseIdentifier,
    Urgency,
    Category,
    SupplementalCategory,
    Instructions,
    CreatorTitle,
    TransmissionReference,
    CaptionWriter,
    AccessibilityAltText,
    AccessibilityExtendedDescription,
    IntellectualGenre,
    SceneCode,
    SubjectCode,
    ResourceIdentifier,
    DerivedFromIdentifier,
    DocumentIdentifier,
    InstanceIdentifier,
    OriginalDocumentIdentifier,
    RenditionClass,
    ImageIdentifier,
    Notes,
    MediaSummaryCode,
    ImageDuplicationConstraint,
    MinorModelAgeDisclosure,
    AdultContentWarning,
    DeliveredImageType,
    DeliveredFileName,
    DeliveredFileFormat,
    DeliveredFileSize,
    CopyrightRegistrationNumber,
    FirstPublicationDate,
    OtherImageInformation,
    Reuse,
    DataMining,
    OtherLicenseDocument,
    OtherLicenseInformation,
};

enum class MetadataConceptRecordKind : uint8_t {
    None,
    CreatorContact,
    Event,
    Person,
    Organization,
    Product,
    ArtworkOrObject,
    RightsExpression,
    RightsHolder,
    Licensor,
    Licensee,
    License,
    Release,
    EndUser,
    ImageCreator,
    ImageSupplier,
    ImageAsset,
};

/// Policy sensitivity is independent from technical transfer safety.
enum class MetadataConceptSensitivity : uint8_t {
    None,
    PersonalContact,
    PersonIdentity,
    Location,
    LegalRights,
};

enum class MetadataConceptDateTimePrecision : uint8_t {
    Unknown,
    Date,
    DateTime,
    DateTimeSubsecond,
};

enum class MetadataConceptTimeZoneKind : uint8_t {
    Unknown,
    Local,
    Utc,
    Offset,
};

enum class MetadataConceptTransferHint : uint8_t {
    Unknown,
    Safe,
    SourceBound,
    RenderedUnsafe,
    RequiresTargetImageSpec,
};

enum class MetadataRawDataEncoding : uint8_t {
    Unknown,
    Uncompressed,
    Packed,
    LosslessCompressed,
    LossyCompressed,
    Rendered,
};

enum class MetadataRawApplicabilityState : uint8_t {
    Unknown,
    AppliesToStoredRaw,
    ConditionalOnRawEncoding,
    NotApplicableToStoredRaw,
};

struct MetadataRawDataDescriptor final {
    MetadataRawDataEncoding encoding      = MetadataRawDataEncoding::Unknown;
    bool has_dimensions                   = false;
    uint32_t width                        = 0U;
    uint32_t height                       = 0U;
    bool has_channel_count                = false;
    uint32_t channel_count                = 0U;
    bool has_bits_per_sample              = false;
    uint32_t bits_per_sample              = 0U;
    bool has_compression_code             = false;
    uint32_t compression_code             = 0U;
    bool has_plane_index                  = false;
    uint32_t plane_index                  = 0U;
    bool requires_compressed_raw_encoding = false;
    bool requires_primary_raw_plane       = false;
};

struct MetadataConceptCandidate final {
    MetadataConceptKind kind           = MetadataConceptKind::Orientation;
    MetadataConceptRole role           = MetadataConceptRole::Primary;
    MetadataConceptSourceFamily family = MetadataConceptSourceFamily::Unknown;
    MetadataQuerySemanticKind semantic = MetadataQuerySemanticKind::Unknown;
    MetadataQueryValueShape shape      = MetadataQueryValueShape::Unknown;
    /// Normalized structured-record type; `None` for unstructured values.
    MetadataConceptRecordKind record_kind = MetadataConceptRecordKind::None;
    /// Host policy signal; independent from technical transfer safety.
    MetadataConceptSensitivity sensitivity = MetadataConceptSensitivity::None;
    EntryId entry_id                       = kInvalidEntryId;
    std::vector<EntryId> source_entries;
    uint8_t priority = 0U;
    bool preferred   = false;
    bool conflict    = false;

    MetadataConceptTransferHint transfer_hint
        = MetadataConceptTransferHint::Unknown;
    bool compatible_file_safe       = false;
    bool rendered_image_safe        = false;
    bool requires_target_image_spec = false;
    bool source_bound               = false;

    MetadataRawApplicabilityState raw_applicability
        = MetadataRawApplicabilityState::Unknown;
    bool raw_applicability_requires_storage_context = false;
    bool raw_applicability_can_affect_decode        = false;

    bool has_numeric      = false;
    uint8_t numeric_count = 0U;
    double numeric[4] {};

    bool has_values = false;
    std::vector<double> values;

    bool has_origin = false;
    double origin[2] {};

    bool has_size = false;
    double size[2] {};

    bool has_rect = false;
    /// Rect is normalized as x, y, width, height.
    double rect[4] {};

    bool has_margins = false;
    /// Margins are normalized as left, top, right, bottom.
    double margins[4] {};

    std::string text;
    /// Normalized value used for same-role conflict checks.
    std::string value_key;

    bool has_date_time            = false;
    bool date_time_has_time       = false;
    bool date_time_has_utc_offset = false;
    MetadataConceptDateTimePrecision date_time_precision
        = MetadataConceptDateTimePrecision::Unknown;
    MetadataConceptTimeZoneKind date_time_zone
        = MetadataConceptTimeZoneKind::Unknown;
    int16_t date_time_year       = 0;
    uint8_t date_time_month      = 0U;
    uint8_t date_time_day        = 0U;
    uint8_t date_time_hour       = 0U;
    uint8_t date_time_minute     = 0U;
    uint8_t date_time_second     = 0U;
    bool date_time_has_subsecond = false;
    /// Normalized decimal digits, bounded to nanosecond precision.
    std::string date_time_subsecond;
    int16_t date_time_utc_offset_min = 0;

    bool has_gps_altitude_reference     = false;
    bool gps_altitude_below_sea_level   = false;
    uint8_t gps_altitude_reference_code = 0U;
    /// Structured XMP parent path, for example `LocationShown[1]`.
    std::string location_scope;
    /// Structured metadata parent path, for example `Licensor[1]`.
    std::string record_scope;
    /// Normalized language tag for localized descriptive values.
    std::string language;
};

struct MetadataConceptResolution final {
    MetadataConceptKind kind = MetadataConceptKind::Orientation;
    bool found               = false;
    bool conflict            = false;
    EntryId preferred_entry  = kInvalidEntryId;
    std::vector<EntryId> source_entries;
    std::vector<MetadataConceptCandidate> candidates;
};

struct MetadataConceptResult final {
    std::vector<MetadataConceptResolution> concepts;
};

MetadataConceptResolution
resolve_metadata_concept(const MetaStore& store, MetadataConceptKind kind);

MetadataConceptResolution
resolve_metadata_concept(const MetaStore& store, MetadataConceptKind kind,
                         const MetadataRawDataDescriptor& raw_descriptor);

MetadataConceptResult
resolve_metadata_concepts(const MetaStore& store);

MetadataConceptResult
resolve_metadata_concepts(const MetaStore& store,
                          const MetadataRawDataDescriptor& raw_descriptor);

MetadataRawApplicabilityState
metadata_raw_applicability_for_descriptor(
    MetadataConceptRole role,
    const MetadataRawDataDescriptor& descriptor) noexcept;

const char*
metadata_concept_kind_name(MetadataConceptKind kind) noexcept;

const char*
metadata_concept_source_family_name(MetadataConceptSourceFamily family) noexcept;

const char*
metadata_concept_role_name(MetadataConceptRole role) noexcept;

const char*
metadata_concept_record_kind_name(MetadataConceptRecordKind kind) noexcept;

const char*
metadata_concept_sensitivity_name(
    MetadataConceptSensitivity sensitivity) noexcept;

const char*
metadata_concept_datetime_precision_name(
    MetadataConceptDateTimePrecision precision) noexcept;

const char*
metadata_concept_timezone_kind_name(MetadataConceptTimeZoneKind kind) noexcept;

const char*
metadata_concept_transfer_hint_name(MetadataConceptTransferHint hint) noexcept;

const char*
metadata_raw_data_encoding_name(MetadataRawDataEncoding encoding) noexcept;

const char*
metadata_raw_applicability_state_name(
    MetadataRawApplicabilityState state) noexcept;

const char*
metadata_concept_gps_altitude_reference_name(uint8_t code) noexcept;

}  // namespace openmeta
