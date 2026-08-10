// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/container_payload.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/exr_decode.h"
#include "openmeta/icc_decode.h"
#include "openmeta/iptc_iim_decode.h"
#include "openmeta/jumbf_decode.h"
#include "openmeta/photoshop_irb_decode.h"
#include "openmeta/preview_extract.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <cstdint>

/**
 * \file resource_policy.h
 * \brief Draft resource-budget policy for OpenMeta read/dump workflows.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/**
 * \brief Draft, storage-agnostic resource limits for untrusted metadata input.
 *
 * This policy intentionally favors parser/output budgets over hard file-size
 * caps, so large legitimate assets (for example RAW/EXR) can still be
 * processed when decode limits are respected.
 */
struct OpenMetaResourcePolicy final {
    /// Optional file mapping cap (0 = unlimited).
    uint64_t max_file_bytes = 0;

    /// Reassembly/decompression budgets.
    PayloadLimits payload_limits;

    /// EXIF/TIFF decode budgets.
    ExifDecodeLimits exif_limits;

    /// XMP RDF/XML decode budgets.
    XmpDecodeLimits xmp_limits;

    /// OpenEXR header decode budgets.
    ExrDecodeLimits exr_limits;

    /// JUMBF/C2PA decode budgets.
    JumbfDecodeLimits jumbf_limits;

    /// ICC profile decode budgets.
    IccDecodeLimits icc_limits;

    /// IPTC-IIM decode budgets.
    IptcIimDecodeLimits iptc_limits;

    /// Photoshop IRB decode budgets.
    PhotoshopIrbDecodeLimits photoshop_irb_limits;

    /// Embedded preview discovery/extraction budgets.
    PreviewScanLimits preview_scan_limits;
    uint64_t max_preview_output_bytes = 128ULL * 1024ULL * 1024ULL;

    /// XMP sidecar dump budgets.
    XmpDumpLimits xmp_dump_limits;

    /// Draft future budget hooks (not enforced yet).
    uint32_t max_decode_millis           = 0;
    uint32_t max_decompression_ratio     = 0;
    uint64_t max_total_decode_work_bytes = 0;
};

/**
 * \brief Normalizes traversal/recursion-related limits to safe defaults.
 *
 * OpenMeta design goal: recursion/traversal limits should never become
 * "unlimited" through a zero value in user policy. This helper keeps decode
 * paths bounded for untrusted metadata.
 */
inline void
normalize_resource_policy(OpenMetaResourcePolicy* policy) noexcept
{
    if (!policy) {
        return;
    }

    if (policy->payload_limits.max_parts == 0U) {
        policy->payload_limits.max_parts = 1U << 14;
    }

    if (policy->exif_limits.max_ifds == 0U) {
        policy->exif_limits.max_ifds = 128U;
    }
    if (policy->exif_limits.max_entries_per_ifd == 0U) {
        policy->exif_limits.max_entries_per_ifd = 4096U;
    }
    if (policy->exif_limits.max_total_entries == 0U) {
        policy->exif_limits.max_total_entries = 200000U;
    }

    if (policy->xmp_limits.max_depth == 0U) {
        policy->xmp_limits.max_depth = 128U;
    }
    if (policy->xmp_limits.max_properties == 0U) {
        policy->xmp_limits.max_properties = 200000U;
    }

    if (policy->exr_limits.max_parts == 0U) {
        policy->exr_limits.max_parts = 64U;
    }
    if (policy->exr_limits.max_attributes_per_part == 0U) {
        policy->exr_limits.max_attributes_per_part = 1U << 16;
    }
    if (policy->exr_limits.max_attributes == 0U) {
        policy->exr_limits.max_attributes = 200000U;
    }

    if (policy->jumbf_limits.max_box_depth == 0U) {
        policy->jumbf_limits.max_box_depth = 32U;
    }
    if (policy->jumbf_limits.max_boxes == 0U) {
        policy->jumbf_limits.max_boxes = 1U << 16;
    }
    if (policy->jumbf_limits.max_entries == 0U) {
        policy->jumbf_limits.max_entries = 200000U;
    }
    if (policy->jumbf_limits.max_cbor_depth == 0U) {
        policy->jumbf_limits.max_cbor_depth = 64U;
    }
    if (policy->jumbf_limits.max_cbor_items == 0U) {
        policy->jumbf_limits.max_cbor_items = 200000U;
    }

    if (policy->iptc_limits.max_datasets == 0U) {
        policy->iptc_limits.max_datasets = 200000U;
    }

    if (policy->photoshop_irb_limits.max_resources == 0U) {
        policy->photoshop_irb_limits.max_resources = 1U << 16;
    }

    if (policy->preview_scan_limits.max_ifds == 0U) {
        policy->preview_scan_limits.max_ifds = 256U;
    }
    if (policy->preview_scan_limits.max_total_entries == 0U) {
        policy->preview_scan_limits.max_total_entries = 8192U;
    }
}

inline OpenMetaResourcePolicy
recommended_resource_policy() noexcept
{
    OpenMetaResourcePolicy policy;
    normalize_resource_policy(&policy);
    return policy;
}

inline void
apply_resource_policy(const OpenMetaResourcePolicy& policy,
                      ExifDecodeOptions* exif, PayloadOptions* payload) noexcept
{
    OpenMetaResourcePolicy normalized = policy;
    normalize_resource_policy(&normalized);
    if (exif) {
        exif->limits = normalized.exif_limits;
    }
    if (payload) {
        payload->limits = normalized.payload_limits;
    }
}

inline void
apply_resource_policy(const OpenMetaResourcePolicy& policy,
                      XmpDecodeOptions* xmp, ExrDecodeOptions* exr,
                      JumbfDecodeOptions* jumbf, IccDecodeOptions* icc,
                      IptcIimDecodeOptions* iptc,
                      PhotoshopIrbDecodeOptions* irb) noexcept
{
    OpenMetaResourcePolicy normalized = policy;
    normalize_resource_policy(&normalized);
    if (xmp) {
        xmp->limits = normalized.xmp_limits;
    }
    if (exr) {
        exr->limits = normalized.exr_limits;
    }
    if (jumbf) {
        jumbf->limits = normalized.jumbf_limits;
    }
    if (icc) {
        icc->limits = normalized.icc_limits;
    }
    if (iptc) {
        iptc->limits = normalized.iptc_limits;
    }
    if (irb) {
        irb->limits      = normalized.photoshop_irb_limits;
        irb->iptc.limits = normalized.iptc_limits;
    }
}

inline void
apply_resource_policy(const OpenMetaResourcePolicy& policy,
                      XmpDecodeOptions* xmp, ExrDecodeOptions* exr,
                      IccDecodeOptions* icc, IptcIimDecodeOptions* iptc,
                      PhotoshopIrbDecodeOptions* irb) noexcept
{
    apply_resource_policy(policy, xmp, exr, nullptr, icc, iptc, irb);
}

inline void
apply_resource_policy(const OpenMetaResourcePolicy& policy,
                      PreviewScanOptions* scan,
                      PreviewExtractOptions* extract) noexcept
{
    OpenMetaResourcePolicy normalized = policy;
    normalize_resource_policy(&normalized);
    if (scan) {
        scan->limits = normalized.preview_scan_limits;
    }
    if (extract) {
        extract->max_output_bytes = normalized.max_preview_output_bytes;
    }
}

inline void
apply_resource_policy(const OpenMetaResourcePolicy& policy,
                      XmpSidecarRequest* request) noexcept
{
    OpenMetaResourcePolicy normalized = policy;
    normalize_resource_policy(&normalized);
    if (request) {
        request->limits = normalized.xmp_dump_limits;
    }
}

}  // namespace openmeta
OPENMETA_PUBLIC_END
