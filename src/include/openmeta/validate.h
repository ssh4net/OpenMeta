// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/ccm_query.h"
#include "openmeta/jumbf_decode.h"
#include "openmeta/meta_store.h"
#include "openmeta/resource_policy.h"
#include "openmeta/simple_meta.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * \file validate.h
 * \brief High-level metadata validation API (decode health + DNG/CCM checks).
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable detached entry/store validation contract version.
inline constexpr uint32_t kMetadataValidationContractVersion = 1U;

/// Top-level validation status for \ref validate_file.
enum class ValidateStatus : uint8_t {
    Ok,
    OpenFailed,
    TooLarge,
    ReadFailed,
};

/// Validation issue severity.
enum class ValidateIssueSeverity : uint8_t {
    Warning,
    Error,
};

/// Stable result status for detached metadata entry/store validation.
enum class MetadataValidationStatus : uint8_t {
    Ok,
    InvalidArgument,
    InvalidMetadata,
    LimitExceeded,
};

/// Policy for EXIF/TIFF tags not present in the built-in standard schema table.
enum class MetadataUnknownTagPolicy : uint8_t {
    Allow,
    Warning,
    Error,
};

/// Stable detached metadata validation issue code.
enum class MetadataValidationIssueCode : uint16_t {
    None = 0,
    StoreNotFinalized,
    InvalidEntryId,
    StoreLimitExceeded,
    ValueLimitExceeded,
    InvalidKey,
    InvalidOrigin,
    InvalidValueShape,
    ScalarOutOfRange,
    RationalDenominatorZero,
    InvalidText,
    InvalidWireType,
    InvalidWireCount,
    WrongIfd,
    WrongType,
    WrongCount,
    DuplicateSingleton,
    InvalidXmpNamespace,
    InvalidXmpPropertyPath,
    UnknownExifTag,
    ImageContextMismatch,
    InconsistentRelatedEntries,
    IssueLimitExceeded,
};

/// Optional destination/raw-image facts used for cross-entry validation.
struct MetadataValidationContext final {
    bool has_dimensions = false;
    uint32_t width      = 0U;
    uint32_t height     = 0U;

    bool has_samples_per_pixel = false;
    uint16_t samples_per_pixel = 0U;

    bool has_color_planes = false;
    uint16_t color_planes = 0U;
};

/// Policy and resource bounds for detached entry/store validation.
struct MetadataValidationOptions final {
    bool validate_schema     = true;
    bool validate_wire_hints = true;
    bool require_finalized   = false;
    bool warnings_as_errors  = false;
    uint32_t max_issues      = 4096U;
    uint32_t max_key_bytes   = 4096U;
    uint32_t max_entries     = 200000U;
    uint64_t max_arena_bytes = 64ULL * 1024ULL * 1024ULL;
    uint64_t max_value_bytes = 64ULL * 1024ULL * 1024ULL;
    MetadataUnknownTagPolicy unknown_exif_tags = MetadataUnknownTagPolicy::Allow;
    MetadataValidationContext context;
};

/// One fixed-field structured issue from detached validation.
struct MetadataValidationIssue final {
    ValidateIssueSeverity severity   = ValidateIssueSeverity::Warning;
    MetadataValidationIssueCode code = MetadataValidationIssueCode::None;
    EntryId entry                    = kInvalidEntryId;
    EntryId related_entry            = kInvalidEntryId;
    MetaKeyKind key_kind             = MetaKeyKind::ExifTag;
    uint16_t tag                     = 0U;
};

/// Detached entry/store validation result.
struct MetadataValidationResult final {
    MetadataValidationStatus status = MetadataValidationStatus::Ok;
    uint32_t entries_checked        = 0U;
    uint32_t warning_count          = 0U;
    uint32_t error_count            = 0U;
    std::vector<MetadataValidationIssue> issues;

    bool ok() const noexcept
    {
        return status == MetadataValidationStatus::Ok && error_count == 0U;
    }
};

/// One validation issue emitted by \ref validate_file.
struct ValidateIssue final {
    ValidateIssueSeverity severity = ValidateIssueSeverity::Warning;
    /// Domain/category (`scan`, `exif`, `ccm`, `file`, ...).
    std::string category;
    /// Stable issue token (`malformed`, `limit_exceeded`, ...).
    std::string code;
    /// Optional source IFD token (for CCM issues).
    std::string ifd;
    /// Optional source field/tag name (for CCM issues).
    std::string name;
    /// Optional source tag id (for CCM issues).
    uint16_t tag = 0;
    /// Human-readable details.
    std::string message;
};

/// Options for \ref validate_file.
struct ValidateOptions final {
    bool include_pointer_tags               = true;
    bool decode_makernote                   = false;
    bool decode_printim                     = true;
    bool decompress                         = true;
    bool include_xmp_sidecar                = false;
    bool verify_c2pa                        = false;
    C2paVerifyBackend verify_backend        = C2paVerifyBackend::Auto;
    bool verify_require_trusted_chain       = false;
    bool verify_require_resolved_references = false;

    /// Treat warnings as failures in \ref ValidateResult::failed.
    bool warnings_as_errors = false;

    /// DNG/CCM query + validation options.
    CcmQueryOptions ccm;

    /// Resource budgets for decode/scans.
    OpenMetaResourcePolicy policy;
};

/// Result of \ref validate_file.
struct ValidateResult final {
    ValidateStatus status = ValidateStatus::Ok;
    uint64_t file_size    = 0;

    /// Decode summary from \ref simple_meta_read.
    SimpleMetaResult read;
    /// CCM query summary from \ref collect_dng_ccm_fields.
    CcmQueryResult ccm;
    /// Number of extracted CCM fields.
    uint32_t ccm_fields = 0;
    /// Final decoded entry count.
    uint32_t entries = 0;

    uint32_t warning_count = 0;
    uint32_t error_count   = 0;
    bool failed            = false;

    std::vector<ValidateIssue> issues;
};

/**
 * \brief Validate one file using normal OpenMeta decode + CCM checks.
 *
 * Validation covers:
 * - decoder status health (`scan/payload/exif/xmp/exr/jumbf/c2pa`)
 * - optional sidecar XMP read status (`--xmp-sidecar` equivalent)
 * - DNG/CCM query/validation issues (`collect_dng_ccm_fields`)
 */
ValidateResult
validate_file(const char* path,
              const ValidateOptions& options = ValidateOptions {}) noexcept;

/** Validate one entry in a detached store without modifying it. */
MetadataValidationResult
validate_entry(const MetaStore& store, EntryId entry,
               const MetadataValidationOptions& options
               = MetadataValidationOptions {}) noexcept;

/** Validate structural, schema, duplicate, and optional image relationships. */
MetadataValidationResult
validate_store(const MetaStore& store, const MetadataValidationOptions& options
                                       = MetadataValidationOptions {}) noexcept;

const char*
metadata_validation_status_name(MetadataValidationStatus status) noexcept;
const char*
metadata_validation_issue_code_name(MetadataValidationIssueCode code) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
