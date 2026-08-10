// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/interop_export.h"
#include "openmeta/metadata_transfer.h"

#include <cstdint>
#include <string>

/**
 * \file compatibility_dump.h
 * \brief Deterministic text dumps for host compatibility tests.
 *
 * \par API Stability
 * Stable host-facing v1 dump contract.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable compatibility dump contract version.
inline constexpr uint32_t kCompatibilityDumpContractVersion = 1U;

/// Options for \ref dump_metadata_compatibility.
struct MetadataCompatibilityDumpOptions final {
    ExportNameStyle style        = ExportNameStyle::FlatHost;
    ExportNamePolicy name_policy = ExportNamePolicy::ExifToolAlias;
    bool include_values          = true;
    bool include_origins         = true;
    bool include_flags           = true;
    uint32_t max_value_bytes     = 256U;
};

/// Options for \ref dump_transfer_compatibility.
struct TransferCompatibilityDumpOptions final {
    bool include_prepared_blocks   = true;
    bool include_policy_decisions  = true;
    bool include_writeback_summary = true;
    uint32_t max_message_bytes     = 256U;
};

/**
 * \brief Emit a deterministic line-oriented metadata compatibility dump.
 *
 * The dump includes exported names, value kinds, scalar element types, optional
 * value text, optional origin fields, and optional flags.
 */
bool
dump_metadata_compatibility(const MetaStore& store,
                            const MetadataCompatibilityDumpOptions& options,
                            std::string* out) noexcept;

/**
 * \brief Emit deterministic transfer and writeback decision summaries.
 *
 * Pass \p persisted when file persistence results should be included.
 */
bool
dump_transfer_compatibility(
    const ExecutePreparedTransferFileResult& result,
    const PersistPreparedTransferFileResult* persisted,
    const TransferCompatibilityDumpOptions& options,
    std::string* out) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
