// SPDX-License-Identifier: Apache-2.0

#include "openmeta/resource_policy.h"

#include <gtest/gtest.h>

namespace openmeta {
namespace {

    TEST(ResourcePolicy, RecommendedPolicyHasBoundedTraversalDefaults)
    {
        const OpenMetaResourcePolicy policy = recommended_resource_policy();

        EXPECT_EQ(policy.payload_limits.max_parts, (1U << 14));

        EXPECT_EQ(policy.exif_limits.max_ifds, 128U);
        EXPECT_EQ(policy.exif_limits.max_entries_per_ifd, 4096U);
        EXPECT_EQ(policy.exif_limits.max_total_entries, 200000U);

        EXPECT_EQ(policy.xmp_limits.max_depth, 128U);
        EXPECT_EQ(policy.xmp_limits.max_properties, 200000U);

        EXPECT_EQ(policy.exr_limits.max_parts, 64U);
        EXPECT_EQ(policy.exr_limits.max_attributes_per_part, (1U << 16));
        EXPECT_EQ(policy.exr_limits.max_attributes, 200000U);

        EXPECT_EQ(policy.jumbf_limits.max_box_depth, 32U);
        EXPECT_EQ(policy.jumbf_limits.max_boxes, (1U << 16));
        EXPECT_EQ(policy.jumbf_limits.max_entries, 200000U);
        EXPECT_EQ(policy.jumbf_limits.max_cbor_depth, 64U);
        EXPECT_EQ(policy.jumbf_limits.max_cbor_items, 200000U);
        EXPECT_EQ(policy.jumbf_limits.max_semantic_work,
                  JumbfDecodeLimits::kDefaultMaxSemanticWork);

        EXPECT_EQ(policy.iptc_limits.max_datasets, 200000U);
        EXPECT_EQ(policy.photoshop_irb_limits.max_resources, (1U << 16));
        EXPECT_EQ(policy.preview_scan_limits.max_ifds, 256U);
        EXPECT_EQ(policy.preview_scan_limits.max_total_entries, 8192U);
    }

    TEST(ResourcePolicy, ApplyPolicyNormalizesZeroTraversalLimits)
    {
        OpenMetaResourcePolicy policy;
        policy.payload_limits.max_parts              = 0U;
        policy.exif_limits.max_ifds                  = 0U;
        policy.exif_limits.max_entries_per_ifd       = 0U;
        policy.exif_limits.max_total_entries         = 0U;
        policy.xmp_limits.max_depth                  = 0U;
        policy.xmp_limits.max_properties             = 0U;
        policy.exr_limits.max_parts                  = 0U;
        policy.exr_limits.max_attributes_per_part    = 0U;
        policy.exr_limits.max_attributes             = 0U;
        policy.jumbf_limits.max_box_depth            = 0U;
        policy.jumbf_limits.max_boxes                = 0U;
        policy.jumbf_limits.max_entries              = 0U;
        policy.jumbf_limits.max_semantic_work        = 0U;
        policy.jumbf_limits.max_cbor_depth           = 0U;
        policy.jumbf_limits.max_cbor_items           = 0U;
        policy.iptc_limits.max_datasets              = 0U;
        policy.photoshop_irb_limits.max_resources    = 0U;
        policy.preview_scan_limits.max_ifds          = 0U;
        policy.preview_scan_limits.max_total_entries = 0U;

        ExifDecodeOptions exif;
        PayloadOptions payload;
        apply_resource_policy(policy, &exif, &payload);
        EXPECT_EQ(payload.limits.max_parts, (1U << 14));
        EXPECT_EQ(exif.limits.max_ifds, 128U);
        EXPECT_EQ(exif.limits.max_entries_per_ifd, 4096U);
        EXPECT_EQ(exif.limits.max_total_entries, 200000U);

        XmpDecodeOptions xmp;
        ExrDecodeOptions exr;
        JumbfDecodeOptions jumbf;
        IccDecodeOptions icc;
        IptcIimDecodeOptions iptc;
        PhotoshopIrbDecodeOptions irb;
        apply_resource_policy(policy, &xmp, &exr, &jumbf, &icc, &iptc, &irb);
        EXPECT_EQ(xmp.limits.max_depth, 128U);
        EXPECT_EQ(xmp.limits.max_properties, 200000U);
        EXPECT_EQ(exr.limits.max_parts, 64U);
        EXPECT_EQ(exr.limits.max_attributes_per_part, (1U << 16));
        EXPECT_EQ(exr.limits.max_attributes, 200000U);
        EXPECT_EQ(jumbf.limits.max_box_depth, 32U);
        EXPECT_EQ(jumbf.limits.max_boxes, (1U << 16));
        EXPECT_EQ(jumbf.limits.max_entries, 200000U);
        EXPECT_EQ(jumbf.limits.max_semantic_work,
                  JumbfDecodeLimits::kDefaultMaxSemanticWork);
        EXPECT_EQ(jumbf.limits.max_cbor_depth, 64U);
        EXPECT_EQ(jumbf.limits.max_cbor_items, 200000U);
        EXPECT_EQ(iptc.limits.max_datasets, 200000U);
        EXPECT_EQ(irb.limits.max_resources, (1U << 16));
        EXPECT_EQ(irb.iptc.limits.max_datasets, 200000U);

        PreviewScanOptions scan;
        PreviewExtractOptions extract;
        apply_resource_policy(policy, &scan, &extract);
        EXPECT_EQ(scan.limits.max_ifds, 256U);
        EXPECT_EQ(scan.limits.max_total_entries, 8192U);
        EXPECT_EQ(extract.max_output_bytes, policy.max_preview_output_bytes);
    }

    TEST(ResourcePolicy, ApplyPolicyPreservesExplicitNonZeroValues)
    {
        OpenMetaResourcePolicy policy;
        policy.payload_limits.max_parts              = 123U;
        policy.exif_limits.max_ifds                  = 9U;
        policy.exif_limits.max_entries_per_ifd       = 111U;
        policy.exif_limits.max_total_entries         = 222U;
        policy.xmp_limits.max_depth                  = 17U;
        policy.xmp_limits.max_properties             = 999U;
        policy.exr_limits.max_parts                  = 3U;
        policy.exr_limits.max_attributes_per_part    = 77U;
        policy.exr_limits.max_attributes             = 88U;
        policy.jumbf_limits.max_box_depth            = 5U;
        policy.jumbf_limits.max_boxes                = 66U;
        policy.jumbf_limits.max_entries              = 77U;
        policy.jumbf_limits.max_semantic_work        = 4321U;
        policy.jumbf_limits.max_cbor_depth           = 7U;
        policy.jumbf_limits.max_cbor_items           = 44U;
        policy.iptc_limits.max_datasets              = 101U;
        policy.photoshop_irb_limits.max_resources    = 202U;
        policy.preview_scan_limits.max_ifds          = 11U;
        policy.preview_scan_limits.max_total_entries = 33U;
        policy.max_preview_output_bytes              = 4096U;

        ExifDecodeOptions exif;
        PayloadOptions payload;
        apply_resource_policy(policy, &exif, &payload);
        EXPECT_EQ(payload.limits.max_parts, 123U);
        EXPECT_EQ(exif.limits.max_ifds, 9U);
        EXPECT_EQ(exif.limits.max_entries_per_ifd, 111U);
        EXPECT_EQ(exif.limits.max_total_entries, 222U);

        XmpDecodeOptions xmp;
        ExrDecodeOptions exr;
        JumbfDecodeOptions jumbf;
        IccDecodeOptions icc;
        IptcIimDecodeOptions iptc;
        PhotoshopIrbDecodeOptions irb;
        apply_resource_policy(policy, &xmp, &exr, &jumbf, &icc, &iptc, &irb);
        EXPECT_EQ(xmp.limits.max_depth, 17U);
        EXPECT_EQ(xmp.limits.max_properties, 999U);
        EXPECT_EQ(exr.limits.max_parts, 3U);
        EXPECT_EQ(exr.limits.max_attributes_per_part, 77U);
        EXPECT_EQ(exr.limits.max_attributes, 88U);
        EXPECT_EQ(jumbf.limits.max_box_depth, 5U);
        EXPECT_EQ(jumbf.limits.max_boxes, 66U);
        EXPECT_EQ(jumbf.limits.max_entries, 77U);
        EXPECT_EQ(jumbf.limits.max_semantic_work, 4321U);
        EXPECT_EQ(jumbf.limits.max_cbor_depth, 7U);
        EXPECT_EQ(jumbf.limits.max_cbor_items, 44U);
        EXPECT_EQ(iptc.limits.max_datasets, 101U);
        EXPECT_EQ(irb.limits.max_resources, 202U);
        EXPECT_EQ(irb.iptc.limits.max_datasets, 101U);

        PreviewScanOptions scan;
        PreviewExtractOptions extract;
        apply_resource_policy(policy, &scan, &extract);
        EXPECT_EQ(scan.limits.max_ifds, 11U);
        EXPECT_EQ(scan.limits.max_total_entries, 33U);
        EXPECT_EQ(extract.max_output_bytes, 4096U);
    }

    TEST(ResourcePolicy, ApplyPolicyCoversSidecarAndPreviewOutputPaths)
    {
        OpenMetaResourcePolicy policy;
        policy.max_preview_output_bytes         = 8192U;
        policy.xmp_dump_limits.max_entries      = 42U;
        policy.xmp_dump_limits.max_output_bytes = 65535U;

        PreviewScanOptions scan;
        PreviewExtractOptions extract;
        apply_resource_policy(policy, &scan, &extract);
        EXPECT_EQ(extract.max_output_bytes, 8192U);

        XmpSidecarRequest request;
        apply_resource_policy(policy, &request);
        EXPECT_EQ(request.limits.max_entries, 42U);
        EXPECT_EQ(request.limits.max_output_bytes, 65535U);
    }

}  // namespace
}  // namespace openmeta
