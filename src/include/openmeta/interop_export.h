// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstdint>
#include <string>
#include <string_view>

/**
 * \file interop_export.h
 * \brief Metadata export traversal API for interop adapters.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable interop export naming contract version.
inline constexpr uint32_t kInteropExportContractVersion = 1U;

/// Stable FlatHost naming contract version.
inline constexpr uint32_t kFlatHostExportContractVersion = 1U;

/// Status for strict safe interop export APIs.
enum class InteropSafetyStatus : uint8_t {
    Ok,
    InvalidArgument,
    UnsafeData,
    InternalError,
};

/// Reason code for \ref InteropSafetyError.
enum class InteropSafetyReason : uint8_t {
    None,
    UnsafeBytes,
    InvalidTextEncoding,
    UnsafeTextControlCharacter,
    InternalMismatch,
};

/// Structured error details returned by strict safe interop exports.
struct InteropSafetyError final {
    InteropSafetyReason reason = InteropSafetyReason::None;
    std::string field_name;
    std::string key_path;
    std::string message;
};

/**
 * \brief Key naming policy used by \ref visit_metadata.
 *
 * \par API Stability
 * Stable enum shape. `Canonical`, `XmpPortable`, and `FlatHost` naming
 * contracts are stable.
 */
enum class ExportNameStyle : uint8_t {
    /// Stable, key-space-aware names (for example: `exif:ifd0:0x010F`).
    Canonical,
    /// Portable XMP-like names (for example: `tiff:Make`, `exif:ExposureTime`).
    XmpPortable,
    /// Stable v1 flat host-attribute names (`Make`, `Exif:ExposureTime`).
    FlatHost,
};

/**
 * \brief Name normalization policy for interop exports.
 */
enum class ExportNamePolicy : uint8_t {
    /// Preserve native OpenMeta/EXIF naming (spec-oriented).
    Spec,
    /// Apply ExifTool-compatible aliases and filtering for parity workflows.
    ExifToolAlias,
};

/**
 * \brief Export controls for \ref visit_metadata.
 *
 */
struct ExportOptions final {
    ExportNameStyle style        = ExportNameStyle::Canonical;
    ExportNamePolicy name_policy = ExportNamePolicy::ExifToolAlias;
    bool include_origin          = false;
    bool include_flags           = false;
    bool include_makernotes      = true;
};

/**
 * \brief A single exported metadata item.
 *
 * The \ref name view is valid only for the duration of \ref MetadataSink::on_item.
 */
struct ExportItem final {
    std::string_view name;
    const Entry* entry   = nullptr;
    const Origin* origin = nullptr;
    EntryFlags flags     = EntryFlags::None;
};

/**
 * \brief Callback sink for \ref visit_metadata.
 *
 * \par API Stability
 * Stable host-facing v1 callback contract.
 */
class MetadataSink {
public:
    virtual ~MetadataSink()                               = default;
    virtual void on_item(const ExportItem& item) noexcept = 0;
};

/**
 * \brief Visits exported metadata entries in store order.
 *
 * Deleted entries are skipped. Name mapping depends on \ref ExportOptions::style.
 *
 * \par API Stability
 * Stable host-facing v1 traversal API. The callback receives borrowed views
 * that are valid only during the call to \ref MetadataSink::on_item.
 */
void
visit_metadata(const MetaStore& store, const ExportOptions& options,
               MetadataSink& sink) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
