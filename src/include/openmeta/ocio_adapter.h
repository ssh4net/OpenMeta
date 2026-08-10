// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/interop_export.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * \file ocio_adapter.h
 * \brief Adapter helpers for OCIO-style metadata trees.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Minimal tree node similar to OCIO FormatMetadata composition.
struct OcioMetadataNode final {
    std::string name;
    std::string value;
    std::vector<OcioMetadataNode> children;
};

/// Options for \ref build_ocio_metadata_tree.
struct OcioAdapterOptions final {
    ExportOptions export_options;
    uint32_t max_value_bytes = 1024;
    bool include_empty       = false;
    /// Append derived normalized DNG CCM fields under `dngnorm:*`.
    bool include_normalized_ccm = true;

    OcioAdapterOptions() noexcept
        : export_options()
    {
        export_options.style              = ExportNameStyle::XmpPortable;
        export_options.include_makernotes = false;
    }
};

/// Stable flat request for OCIO adapter export.
struct OcioAdapterRequest final {
    ExportNameStyle style        = ExportNameStyle::XmpPortable;
    ExportNamePolicy name_policy = ExportNamePolicy::ExifToolAlias;
    bool include_makernotes      = false;
    bool include_origin          = false;
    bool include_flags           = false;
    uint32_t max_value_bytes     = 1024;
    bool include_empty           = false;
    bool include_normalized_ccm  = true;
};

/**
 * \brief Builds a deterministic metadata tree for OCIO-style consumers.
 *
 * Mapping rules:
 * - Item names with a `prefix:name` form become namespace nodes (`prefix`)
 *   with leaf children (`name=value`).
 * - Other names are added as direct leaves under the root.
 */
void
build_ocio_metadata_tree(const MetaStore& store, OcioMetadataNode* root,
                         const OcioAdapterOptions& options) noexcept;

/**
 * \brief Strict safe export for OCIO-style metadata tree.
 *
 * Unlike \ref build_ocio_metadata_tree, this API fails when unsafe payloads
 * are encountered (for example raw bytes or invalid/unsafe text sequences).
 */
InteropSafetyStatus
build_ocio_metadata_tree_safe(const MetaStore& store, OcioMetadataNode* root,
                              const OcioAdapterOptions& options,
                              InteropSafetyError* error) noexcept;

/**
 * \brief Converts \ref OcioAdapterRequest into \ref OcioAdapterOptions.
 */
OcioAdapterOptions
make_ocio_adapter_options(const OcioAdapterRequest& request) noexcept;

/**
 * \brief Builds metadata tree via the stable request model.
 */
void
build_ocio_metadata_tree(const MetaStore& store, OcioMetadataNode* root,
                         const OcioAdapterRequest& request) noexcept;

/**
 * \brief Request-based strict safe export for OCIO-style metadata tree.
 */
InteropSafetyStatus
build_ocio_metadata_tree_safe(const MetaStore& store, OcioMetadataNode* root,
                              const OcioAdapterRequest& request,
                              InteropSafetyError* error) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
