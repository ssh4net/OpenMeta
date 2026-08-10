// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * \file ccm_query.h
 * \brief Query helpers for normalized DNG/RAW color matrix metadata.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Query status for \ref collect_dng_ccm_fields.
enum class CcmQueryStatus : uint8_t {
    Ok,
    /// Input was valid but query limits were exceeded.
    LimitExceeded,
};

/// Validation mode for CCM query normalization.
enum class CcmValidationMode : uint8_t {
    /// Do not emit spec-level diagnostics.
    None,
    /// Emit DNG-oriented structure/coherency diagnostics as warnings.
    DngSpecWarnings,
};

/// Validation issue severity.
enum class CcmIssueSeverity : uint8_t {
    Warning,
    /// Field-level hard-invalid issue (the field is skipped).
    Error,
};

/// Validation issue code for \ref CcmIssue.
enum class CcmIssueCode : uint16_t {
    DecodeFailed,
    NonFiniteValue,
    UnexpectedCount,
    MatrixCountNotDivisibleBy3,
    NonPositiveValue,
    AsShotConflict,
    MissingCompanionTag,
    TripleIlluminantRule,
    CalibrationSignatureMismatch,
    MissingIlluminantData,
    InvalidIlluminantCode,
    WhiteXYOutOfRange,
};

/// Limits for CCM query extraction.
struct CcmQueryLimits final {
    uint32_t max_fields           = 128U;
    uint32_t max_values_per_field = 256U;
};

/// Options for \ref collect_dng_ccm_fields.
struct CcmQueryOptions final {
    /// Require DNG context marker (`DNGVersion`) in the source IFD.
    bool require_dng_context = true;
    /// Include `ReductionMatrix*` tags.
    bool include_reduction_matrices = true;
    /// Validation mode for DNG matrix/tag coherency diagnostics.
    CcmValidationMode validation_mode = CcmValidationMode::DngSpecWarnings;
    CcmQueryLimits limits;
};

/// One normalized CCM/white-balance/calibration field.
struct CcmField final {
    /// Canonical field name (e.g. `ColorMatrix1`).
    std::string name;
    /// Source EXIF IFD token (e.g. `ifd0`).
    std::string ifd;
    /// Source EXIF tag id.
    uint16_t tag = 0;
    /// Logical row count (matrix/vector layout hint).
    uint32_t rows = 0;
    /// Logical column count (matrix/vector layout hint).
    uint32_t cols = 0;
    /// Normalized numeric values (row-major for matrices).
    std::vector<double> values;
};

/// Validation issue emitted by \ref collect_dng_ccm_fields.
struct CcmIssue final {
    CcmIssueSeverity severity = CcmIssueSeverity::Warning;
    CcmIssueCode code         = CcmIssueCode::DecodeFailed;
    std::string ifd;
    std::string name;
    uint16_t tag = 0;
    std::string message;
};

/// Query result for \ref collect_dng_ccm_fields.
struct CcmQueryResult final {
    CcmQueryStatus status    = CcmQueryStatus::Ok;
    uint32_t fields_found    = 0;
    uint32_t fields_dropped  = 0;
    uint32_t issues_reported = 0;
};

/**
 * \brief Extracts normalized DNG/RAW CCM-related fields from a \ref MetaStore.
 *
 * The query scans EXIF tags and emits stable field names for:
 * - `ColorMatrix*`, `ForwardMatrix*`, `CameraCalibration*`
 * - `ReductionMatrix*` (optional)
 * - `AsShotNeutral`, `AsShotWhiteXY`, `AnalogBalance`
 * - `CalibrationIlluminant*`
 */
CcmQueryResult
collect_dng_ccm_fields(const MetaStore& store, std::vector<CcmField>* out,
                       const CcmQueryOptions& options = CcmQueryOptions {},
                       std::vector<CcmIssue>* issues  = nullptr) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
