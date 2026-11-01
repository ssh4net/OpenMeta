// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file icc_serialize.h
 * \brief Deterministic canonical ICC profile serialization.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Stable canonical ICC serialization contract version.
inline constexpr uint32_t kIccProfileSerializeContractVersion = 1U;

/// Stable result status for `serialize_icc_profile()`.
enum class IccProfileSerializeStatus : uint8_t {
    Ok,
    OutputTruncated,
    InvalidOptions,
    StoreNotFinalized,
    InvalidMetadata,
    NoIccData,
    LimitExceeded,
};

/// Policy for bytes 84 through 99 of the ICC profile header.
enum class IccProfileIdPolicy : uint8_t {
    Clear,
    Preserve,
};

/// Resource limits and header policy for canonical ICC serialization.
struct IccProfileSerializeOptions final {
    /// Maximum active ICC tags selected from the store.
    uint32_t max_tags = 65536U;
    /// Maximum raw payload bytes in one ICC tag.
    uint32_t max_tag_bytes = 32U * 1024U * 1024U;
    /// Maximum complete profile bytes, including table and padding.
    uint64_t max_output_bytes = 64ULL * 1024ULL * 1024ULL;
    /// Defaults to clearing the profile ID bytes.
    IccProfileIdPolicy profile_id_policy = IccProfileIdPolicy::Clear;
};

/// Size, coverage, and failure details from canonical ICC serialization.
struct IccProfileSerializeResult final {
    IccProfileSerializeStatus status = IccProfileSerializeStatus::Ok;
    uint64_t written                 = 0U;
    uint64_t needed                  = 0U;
    /// Active tag entries included in the output profile.
    uint32_t tags_serialized = 0U;
    /// Accepted explicit header inputs, including supplied size/signature
    /// fields and an explicit profile ID input cleared by policy. Synthesized
    /// default fields are not counted.
    uint32_t header_fields_serialized = 0U;
    /// Store entry ordinal for a selected invalid or oversized entry.
    EntryId failed_entry = kInvalidEntryId;

    bool ok() const noexcept { return status == IccProfileSerializeStatus::Ok; }
};

/**
 * \brief Serialize active ICC header fields and tags as one profile.
 *
 * The store must be finalized and remain unchanged during the call. Only active
 * ICC entries are selected; deleted entries and other key families are ignored.
 * Tag signatures must be unique and are emitted in ascending order. Every tag
 * value is an opaque ICC typed payload with a nonzero type signature and four
 * zero reserved bytes. This checks structure only; tag and color-space
 * semantics remain the host's responsibility.
 *
 * Header entries use their ICC offsets. Supported offsets are 0, 4, 8, 12, 16,
 * 20, 24, 36, 40, 44, 48, 52, 56, 64, 68, 80, and 84. Required explicit
 * fields are version, device class, data color space, PCS, creation date, and
 * illuminant. The profile size and `acsp` signature are derived. Version major
 * must be 2 or 4; class and color-space signatures must be nonzero; the date
 * must be a real calendar date. Typed illuminants are encoded as exactly
 * representable signed 15.16 values. Other header values use their exact ICC
 * widths, with raw `Bytes` values accepted as an exact-width alternative. Raw
 * `Bytes` values use `U8` elements and `Unknown` text encoding; numeric scalar
 * and array values also use `Unknown` text encoding.
 *
 * Profile ID bytes are zero by default. `Preserve` copies an explicit 16-byte
 * ID supplied at offset 84 without computing or verifying a profile ID.
 * Reserved header bytes and tag padding are zero. No clock, calibration, or
 * transform data is synthesized.
 *
 * Options must be nonzero; `max_output_bytes` must fit in `uint32_t` and
 * `size_t`. Output overlap with the store arena is invalid. Empty output
 * measures the exact size and returns `OutputTruncated`; short output receives
 * the canonical prefix after complete preflight. Other errors leave output
 * unchanged and report zero `written` and `needed`. Preparation may allocate
 * bounded scratch and output storage; allocation failure is not a recoverable
 * serializer status. The host must synchronize all store mutation.
 */
IccProfileSerializeResult
serialize_icc_profile(const MetaStore& store, std::span<std::byte> output,
                      const IccProfileSerializeOptions& options
                      = IccProfileSerializeOptions {}) noexcept;

const char*
icc_profile_serialize_status_name(IccProfileSerializeStatus status) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
