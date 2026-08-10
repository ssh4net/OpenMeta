// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstdint>
#include <string_view>

/**
 * \file vendor_raw_processing.h
 * \brief Conservative vendor RAW-processing metadata classification helpers.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

enum class VendorRawProcessingFamily : uint8_t {
    Sony,
    Canon,
    Nikon,
    Fujifilm,
    Pentax,
    Panasonic,
    Olympus,
    Kodak,
    Minolta,
    Sigma,
    Samsung,
    Ricoh,
    Apple,
    Dji,
    Google,
    Flir,
    Casio,
    Sanyo,
    KyoceraRaw,
    Reconyx,
    Hp,
    Jvc,
    Ge,
    Motorola,
    Nintendo,
    Microsoft,
};

enum class VendorRawProcessingGroup : uint32_t {
    None           = 0U,
    Color          = 1U << 0U,
    WhiteBalance   = 1U << 1U,
    Geometry       = 1U << 2U,
    Storage        = 1U << 3U,
    LensCorrection = 1U << 4U,
    RawData        = 1U << 5U,
    Sensor         = 1U << 6U,
    PrivateTable   = 1U << 7U,
    Preview        = 1U << 8U,
    FaceGeometry   = 1U << 9U,
    Computational  = 1U << 10U,
    Thermal        = 1U << 11U,
    Stitch         = 1U << 12U,
};

struct VendorRawProcessingSummary final {
    uint32_t fields_seen            = 0U;
    uint32_t color_fields           = 0U;
    uint32_t white_balance_fields   = 0U;
    uint32_t geometry_fields        = 0U;
    uint32_t storage_fields         = 0U;
    uint32_t lens_correction_fields = 0U;
    uint32_t raw_data_fields        = 0U;
    uint32_t sensor_fields          = 0U;
    uint32_t private_table_fields   = 0U;
    uint32_t preview_fields         = 0U;
    uint32_t face_geometry_fields   = 0U;
    uint32_t computational_fields   = 0U;
    uint32_t thermal_fields         = 0U;
    uint32_t stitch_fields          = 0U;
};

VendorRawProcessingGroup
classify_vendor_raw_processing_field(std::string_view ifd,
                                     std::string_view name,
                                     uint16_t tag) noexcept;

VendorRawProcessingSummary
vendor_raw_processing_from_store(const MetaStore& store,
                                 VendorRawProcessingFamily family) noexcept;

bool
vendor_raw_processing_group_has(VendorRawProcessingGroup groups,
                                VendorRawProcessingGroup group) noexcept;

const char*
vendor_raw_processing_family_name(VendorRawProcessingFamily family) noexcept;

const char*
vendor_raw_processing_group_name(VendorRawProcessingGroup group) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
