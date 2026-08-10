// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * \file icc_interpret.h
 * \brief Selected ICC tag-name and tag-payload interpretation helpers.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Best-effort status for \ref interpret_icc_tag.
enum class IccTagInterpretStatus : uint8_t {
    Ok,
    Unsupported,
    Malformed,
    LimitExceeded,
};

/// Limits for ICC tag interpretation helpers.
struct IccTagInterpretLimits final {
    uint32_t max_values     = 512U;
    uint32_t max_text_bytes = 4096U;
};

/// Options for \ref interpret_icc_tag.
struct IccTagInterpretOptions final {
    IccTagInterpretLimits limits;
};

/// Best-effort decoded view of an ICC tag payload.
struct IccTagInterpretation final {
    /// ICC tag signature (from tag table).
    uint32_t signature = 0;
    /// Tag display name for signature, when known.
    std::string_view name;
    /// ICC tag type signature as text/fourcc (for example `desc`, `XYZ `, `curv`).
    std::string type;
    /// Decoded text (for text-like tags).
    std::string text;
    /// Decoded numeric values (for XYZ/curve/parametric forms).
    std::vector<double> values;
    /// Optional layout hint for matrix-like values.
    uint32_t rows = 0;
    /// Optional layout hint for matrix-like values.
    uint32_t cols = 0;
};

/**
 * \brief Returns a short display name for an ICC tag signature.
 */
std::string_view
icc_tag_name(uint32_t signature) noexcept;

/**
 * \brief Best-effort interpretation for selected ICC tag payload types.
 *
 * Supported payload type signatures:
 * - `desc` (ASCII profile description)
 * - `text` (ASCII text payload)
 * - `sig ` (embedded 4-byte signature)
 * - `data` (ASCII/binary data payload summary)
 * - `mluc` (multi-localized Unicode)
 * - `ncl2` (named-color table summary)
 * - `dtim` (date/time number)
 * - `view` (viewing conditions summary + numeric values)
 * - `meas` (measurement summary + backing/flare values)
 * - `chrm` (chromaticity coordinates + colorant summary)
 * - `sf32` (s15Fixed16 array)
 * - `uf32` (u16Fixed16 array)
 * - `ui08` (uInt8 array)
 * - `ui16` (uInt16 array)
 * - `ui32` (uInt32 array)
 * - `mft1` / `mft2` (LUT summaries)
 * - `mAB ` / `mBA ` (LUT-A/B structural summaries)
 * - `XYZ ` (s15Fixed16 XYZ triplets)
 * - `curv` (TRC curves)
 * - `para` (parametric curves)
 */
IccTagInterpretStatus
interpret_icc_tag(uint32_t signature, std::span<const std::byte> tag_bytes,
                  IccTagInterpretation* out,
                  const IccTagInterpretOptions& options
                  = IccTagInterpretOptions {}) noexcept;

/**
 * \brief Formats a best-effort human-readable value string for an ICC tag.
 *
 * This helper is intended for CLI/Python display paths. It uses
 * \ref interpret_icc_tag and returns `false` for unsupported/malformed payloads.
 */
bool
format_icc_tag_display_value(uint32_t signature,
                             std::span<const std::byte> tag_bytes,
                             uint32_t max_values, uint32_t max_text_bytes,
                             std::string* out) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
