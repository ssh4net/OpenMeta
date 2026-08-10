// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file jumbf_decode.h
 * \brief Decoder for JUMBF/C2PA payload blocks.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// JUMBF decode result status.
enum class JumbfDecodeStatus : uint8_t {
    Ok,
    /// Input does not look like a JUMBF payload.
    Unsupported,
    /// Input is truncated or structurally invalid.
    Malformed,
    /// Refused due to configured resource limits.
    LimitExceeded,
};

/// Draft C2PA verification backend selection.
enum class C2paVerifyBackend : uint8_t {
    None,
    Auto,
    Native,
    OpenSsl,
};

/// Draft C2PA verification status.
enum class C2paVerifyStatus : uint8_t {
    NotRequested,
    DisabledByBuild,
    BackendUnavailable,
    NoSignatures,
    InvalidSignature,
    VerificationFailed,
    Verified,
    NotImplemented,
};

/// Resource limits for JUMBF/C2PA decode.
struct JumbfDecodeLimits final {
    /// Maximum input bytes to accept (0 = unlimited).
    uint64_t max_input_bytes = 64ULL * 1024ULL * 1024ULL;
    /// Maximum BMFF box depth.
    /// 0 is normalized to a safe default (32).
    uint32_t max_box_depth = 32;
    /// Maximum BMFF boxes to traverse.
    /// 0 is normalized to a safe default (65536).
    uint32_t max_boxes = 1U << 16;
    /// Maximum emitted entries.
    /// 0 is normalized to a safe default (200000).
    uint32_t max_entries = 200000;
    /// Maximum CBOR recursion depth.
    /// 0 is normalized to a safe default (64).
    uint32_t max_cbor_depth = 64;
    /// Maximum CBOR items to parse.
    /// 0 is normalized to a safe default (200000).
    uint32_t max_cbor_items = 200000;
    /// Maximum CBOR string key bytes.
    uint32_t max_cbor_key_bytes = 1024;
    /// Maximum CBOR text value bytes.
    uint32_t max_cbor_text_bytes = 8U * 1024U * 1024U;
    /// Maximum CBOR byte-string value bytes.
    uint32_t max_cbor_bytes_bytes = 8U * 1024U * 1024U;
};

/// Decoder options for \ref decode_jumbf_payload.
struct JumbfDecodeOptions final {
    /// If true, traverse `cbor` boxes and emit decoded CBOR key/value entries.
    bool decode_cbor = true;
    /// If true, emit a `c2pa.detected` marker when C2PA-like payload is seen.
    bool detect_c2pa = true;
    /// If true, request draft C2PA verification scaffold fields.
    bool verify_c2pa = false;
    /// Verification backend preference (used when \ref verify_c2pa is true).
    C2paVerifyBackend verify_backend = C2paVerifyBackend::Auto;
    /// If true, require the certificate chain to validate against the system
    /// trust store. Untrusted or missing chains fail verification even when
    /// the signature matches.
    bool verify_require_trusted_chain = false;
    /// If true, explicit claim references must resolve deterministically:
    /// unresolved or ambiguous explicit-reference signatures fail verification.
    bool verify_require_resolved_references = false;
    JumbfDecodeLimits limits;
};

/// JUMBF decode result summary.
struct JumbfDecodeResult final {
    JumbfDecodeStatus status                  = JumbfDecodeStatus::Unsupported;
    uint32_t boxes_decoded                    = 0;
    uint32_t cbor_items                       = 0;
    uint32_t entries_decoded                  = 0;
    C2paVerifyStatus verify_status            = C2paVerifyStatus::NotRequested;
    C2paVerifyBackend verify_backend_selected = C2paVerifyBackend::None;
};

/// Preflight structural estimate for a JUMBF/C2PA payload.
///
/// This is a scan-only estimate: it does not emit metadata entries.
struct JumbfStructureEstimate final {
    JumbfDecodeStatus status = JumbfDecodeStatus::Unsupported;
    uint32_t boxes_scanned   = 0;
    uint32_t max_box_depth   = 0;
    uint32_t cbor_payloads   = 0;
    uint32_t cbor_items      = 0;
    uint32_t max_cbor_depth  = 0;
};

/**
 * \brief Decodes a JUMBF/C2PA payload and appends entries into \p store.
 *
 * Emitted entries use:
 * - \ref MetaKeyKind::JumbfField for structural fields
 * - \ref MetaKeyKind::JumbfCborKey for decoded CBOR keys/values
 *
 * Duplicate keys are preserved.
 */
JumbfDecodeResult
decode_jumbf_payload(std::span<const std::byte> bytes, MetaStore& store,
                     EntryFlags flags = EntryFlags::None,
                     const JumbfDecodeOptions& options
                     = JumbfDecodeOptions {}) noexcept;

/**
 * \brief Estimates JUMBF decode entry count using the same limits/options.
 */
JumbfDecodeResult
measure_jumbf_payload(std::span<const std::byte> bytes,
                      const JumbfDecodeOptions& options
                      = JumbfDecodeOptions {}) noexcept;

/**
 * \brief Estimates structural nesting for a JUMBF/C2PA payload.
 *
 * Performs bounded BMFF box traversal and bounded CBOR structural parsing to
 * report maximum observed nesting depth and item counts. This function is
 * intended for preflight risk checks before full decode.
 */
JumbfStructureEstimate
measure_jumbf_structure(std::span<const std::byte> bytes,
                        const JumbfDecodeLimits& limits
                        = JumbfDecodeLimits {}) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
