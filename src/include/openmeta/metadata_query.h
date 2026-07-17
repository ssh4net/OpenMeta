// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * \file metadata_query.h
 * \brief Experimental semantic metadata query helpers.
 */

namespace openmeta {

enum class MetadataQueryKind : uint8_t {
    Crop,
    ExposureGain,
    WhiteBalance,
    Color,
    LensCorrection,
    Orientation,
    RawProcessing,
    Descriptive,
};

enum class MetadataQuerySemanticKind : uint8_t {
    Unknown,
    Crop,
    Border,
    ActiveArea,
    Exposure,
    Gain,
    Color,
    ColorProfile,
    WhiteBalance,
    ColorMatrix,
    LensCorrection,
    Orientation,
    ExposureGain,
    BlackLevel,
    WhiteLevel,
    Linearization,
    CfaLayout,
    SensorGeometry,
    RawStorage,
    SourceProcessing,
    ComputationalProcessing,
    ThermalProcessing,
    StitchProcessing,
    Title,
    Description,
    Creator,
    Keywords,
    SourceColorTransform,
    RawValueCurve,
    RawLinearityLimit,
    RawCalibrationCurve,
    RawCurveControlPoints,
    Rights,
    License,
    Credit,
    Source,
    Contact,
    Event,
    Person,
    Organization,
    Product,
    Artwork,
    RightsExpression,
    Release,
};

enum class MetadataQueryValueShape : uint8_t {
    Unknown,
    Scalar,
    Vec2,
    Vec3,
    Vec4,
    Rect,
    Matrix3x3,
    VectorSet,
    MatrixSet,
    Table,
    Array,
    Blob,
    Text,
};

enum class MetadataQueryMatchTerm : uint32_t {
    None             = 0U,
    Crop             = 1U << 0U,
    Border           = 1U << 1U,
    Margin           = 1U << 2U,
    Padding          = 1U << 3U,
    ActiveArea       = 1U << 4U,
    Origin           = 1U << 5U,
    Offset           = 1U << 6U,
    Size             = 1U << 7U,
    Sensor           = 1U << 8U,
    Image            = 1U << 9U,
    Exposure         = 1U << 10U,
    Bias             = 1U << 11U,
    Gain             = 1U << 12U,
    WhiteBalance     = 1U << 13U,
    Color            = 1U << 14U,
    Matrix           = 1U << 15U,
    Calibration      = 1U << 16U,
    Profile          = 1U << 17U,
    Lens             = 1U << 18U,
    Correction       = 1U << 19U,
    Orientation      = 1U << 20U,
    BlackLevel       = 1U << 21U,
    WhiteLevel       = 1U << 22U,
    Linearization    = 1U << 23U,
    Cfa              = 1U << 24U,
    Raw              = 1U << 25U,
    Storage          = 1U << 26U,
    SourceProcessing = 1U << 27U,
    Title            = 1U << 28U,
    Description      = 1U << 29U,
    Creator          = 1U << 30U,
    Keywords         = 1U << 31U,
};

struct MetadataQueryMatch final {
    EntryId entry_id                   = kInvalidEntryId;
    MetaKeyKind key_kind               = MetaKeyKind::ExifTag;
    MetadataQuerySemanticKind semantic = MetadataQuerySemanticKind::Unknown;
    MetadataQueryValueShape shape      = MetadataQueryValueShape::Unknown;
    uint8_t confidence                 = 0U;
    uint32_t matched_terms             = 0U;
    bool exact_match                   = false;
    bool fuzzy_match                   = false;
    uint8_t fuzzy_score                = 0U;
    uint16_t exif_tag                  = 0U;
    std::string group;
    std::string name;
};

struct MetadataQueryCandidate final {
    MetadataQuerySemanticKind semantic = MetadataQuerySemanticKind::Unknown;
    MetadataQueryValueShape normalized_shape = MetadataQueryValueShape::Unknown;
    uint8_t confidence                       = 0U;
    std::vector<EntryId> source_entries;

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

    bool has_values = false;
    std::vector<double> values;
};

struct MetadataQueryResult final {
    MetadataQueryKind kind = MetadataQueryKind::Crop;
    std::vector<MetadataQueryMatch> matches;
    std::vector<MetadataQueryCandidate> candidates;
};

MetadataQueryResult
query_metadata(const MetaStore& store, MetadataQueryKind kind);

MetadataQueryResult
query_crop_metadata(const MetaStore& store);

MetadataQueryResult
query_exposure_gain_metadata(const MetaStore& store);

MetadataQueryResult
query_white_balance_metadata(const MetaStore& store);

MetadataQueryResult
query_color_metadata(const MetaStore& store);

MetadataQueryResult
query_lens_correction_metadata(const MetaStore& store);

MetadataQueryResult
query_orientation_metadata(const MetaStore& store);

MetadataQueryResult
query_raw_processing_metadata(const MetaStore& store);

MetadataQueryResult
query_descriptive_metadata(const MetaStore& store);

bool
metadata_query_fuzzy_search_available() noexcept;

const char*
metadata_query_kind_name(MetadataQueryKind kind) noexcept;

const char*
metadata_query_semantic_kind_name(MetadataQuerySemanticKind kind) noexcept;

const char*
metadata_query_value_shape_name(MetadataQueryValueShape shape) noexcept;

}  // namespace openmeta
