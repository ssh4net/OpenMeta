// SPDX-License-Identifier: Apache-2.0

#include "openmeta/host_adoption.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

TEST(HostAdoptionProfile, ReportsExactV1Contracts)
{
    static_assert(std::is_trivially_copyable_v<openmeta::HostAdoptionProfile>);
    static_assert(sizeof(openmeta::HostAdoptionProfile)
                  == 9U * sizeof(uint32_t));

    const openmeta::HostAdoptionProfile linked
        = openmeta::host_adoption_profile();
    EXPECT_EQ(linked, openmeta::kHostAdoptionProfileV1);
    EXPECT_EQ(linked.profile_version,
              openmeta::kHostAdoptionProfileContractVersion);
    EXPECT_EQ(linked.random_access_source_version,
              openmeta::kRandomAccessSourceContractVersion);
    EXPECT_EQ(linked.positional_snapshot_version,
              openmeta::kReadTransferSourceSnapshotContractVersion);
    EXPECT_EQ(linked.snapshot_object_version,
              openmeta::kTransferSourceSnapshotContractVersion);
    EXPECT_EQ(linked.snapshot_serialization_version,
              openmeta::kTransferSourceSnapshotSerializationVersion);
    EXPECT_EQ(linked.flat_host_export_version,
              openmeta::kFlatHostExportContractVersion);
    EXPECT_EQ(linked.flat_host_import_version,
              openmeta::kFlatHostImportContractVersion);
    EXPECT_EQ(linked.read_diagnostics_version,
              openmeta::kReadTransferSourceDiagnosticsContractVersion);
    EXPECT_EQ(linked.prepared_adapter_schema_version,
              openmeta::kPreparedTransferAdapterContractVersion);
    EXPECT_TRUE(openmeta::host_adoption_profile_matches(
        openmeta::kHostAdoptionProfileV1));

    openmeta::HostAdoptionProfile incompatible = linked;
    incompatible.flat_host_import_version += 1U;
    EXPECT_FALSE(openmeta::host_adoption_profile_matches(incompatible));
}
