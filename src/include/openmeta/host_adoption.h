// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/interop_import.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/random_access_source.h"

#include <cstdint>

/**
 * \file host_adoption.h
 * \brief Versioned compatibility profile for high-performance host integration.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable high-performance host adoption profile version.
inline constexpr uint32_t kHostAdoptionProfileContractVersion = 1U;

/**
 * \brief Exact contract versions that form one host adoption profile.
 *
 * This descriptor reports API compatibility, not format coverage. Query
 * format/family support separately with `metadata_capability(...)`, and inspect
 * positional read residuals before treating a snapshot as complete.
 */
struct HostAdoptionProfile final {
    uint32_t profile_version              = kHostAdoptionProfileContractVersion;
    uint32_t random_access_source_version = kRandomAccessSourceContractVersion;
    uint32_t positional_snapshot_version
        = kReadTransferSourceSnapshotContractVersion;
    uint32_t snapshot_object_version = kTransferSourceSnapshotContractVersion;
    uint32_t snapshot_serialization_version
        = kTransferSourceSnapshotSerializationVersion;
    uint32_t flat_host_export_version = kFlatHostExportContractVersion;
    uint32_t flat_host_import_version = kFlatHostImportContractVersion;
    uint32_t read_diagnostics_version
        = kReadTransferSourceDiagnosticsContractVersion;
    uint32_t prepared_adapter_schema_version
        = kPreparedTransferAdapterContractVersion;

    constexpr bool operator==(const HostAdoptionProfile&) const noexcept
        = default;
};

/// Compile-time descriptor for Host Adoption Profile v1.
inline constexpr HostAdoptionProfile kHostAdoptionProfileV1 {};

/// Returns the profile contract versions compiled into the linked library.
HostAdoptionProfile
host_adoption_profile() noexcept;

/// Checks an exact profile descriptor against the linked library.
bool
host_adoption_profile_matches(const HostAdoptionProfile& expected) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
