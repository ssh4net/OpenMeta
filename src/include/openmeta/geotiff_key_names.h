// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include <cstdint>
#include <string_view>

/**
 * \file geotiff_key_names.h
 * \brief GeoTIFF GeoKey name lookup.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Returns a best-effort GeoTIFF key name for a numeric GeoKey id.
std::string_view
geotiff_key_name(uint16_t key_id) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
