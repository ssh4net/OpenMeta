// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/ccm_query.h"
#include "openmeta/jumbf_decode.h"
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

}  // namespace openmeta
OPENMETA_PUBLIC_END
