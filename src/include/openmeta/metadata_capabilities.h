// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/metadata_transfer.h"

#include <cstdint>

/**
 * \file metadata_capabilities.h
 * \brief Runtime capability query API for host integrations.
 *
 * \par API Stability
 * Stable host-facing v1 query contract.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable metadata capability contract version.
inline constexpr uint32_t kMetadataCapabilitiesContractVersion = 1U;

/// Metadata family used by \ref metadata_capability.
enum class MetadataCapabilityFamily : uint8_t {
    Exif,
    Xmp,
    Icc,
    Iptc,
    MakerNote,
    PhotoshopIrb,
    Jumbf,
    C2pa,
    BmffFields,
    GeoTiff,
    ExrAttribute,
};

/// Support level for one operation in \ref MetadataCapability.
enum class MetadataCapabilitySupport : uint8_t {
    Unsupported,
    Supported,
    Bounded,
    Disabled,
};

/// Capability record for one format/family pair.
struct MetadataCapability final {
    TransferTargetFormat format = TransferTargetFormat::Jpeg;
    MetadataCapabilityFamily family = MetadataCapabilityFamily::Exif;

    MetadataCapabilitySupport read = MetadataCapabilitySupport::Unsupported;
    MetadataCapabilitySupport structured_decode
        = MetadataCapabilitySupport::Unsupported;
    MetadataCapabilitySupport transfer_prepare
        = MetadataCapabilitySupport::Unsupported;
    MetadataCapabilitySupport target_edit
        = MetadataCapabilitySupport::Unsupported;

    /// Existing raw-carrier preservation in current transfer flows. This does
    /// not imply raw-preserving source snapshots.
    MetadataCapabilitySupport raw_preservation
        = MetadataCapabilitySupport::Unsupported;
};

const char*
metadata_capability_family_name(MetadataCapabilityFamily family) noexcept;

const char*
metadata_capability_support_name(MetadataCapabilitySupport support) noexcept;

bool
metadata_capability_available(MetadataCapabilitySupport support) noexcept;

MetadataCapability
metadata_capability(TransferTargetFormat format,
                    MetadataCapabilityFamily family) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
