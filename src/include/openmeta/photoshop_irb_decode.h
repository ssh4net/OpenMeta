// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/icc_decode.h"
#include "openmeta/iptc_iim_decode.h"
#include "openmeta/meta_store.h"
#include "openmeta/xmp_decode.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/**
 * \file photoshop_irb_decode.h
 * \brief Decoder for Photoshop Image Resource Blocks (IRB / 8BIM resources).
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Photoshop IRB decode result status.
enum class PhotoshopIrbDecodeStatus : uint8_t {
    Ok,
    /// The bytes do not look like an IRB stream.
    Unsupported,
    /// The stream is malformed or inconsistent.
    Malformed,
    /// Resource limits were exceeded.
    LimitExceeded,
};

/// Charset policy for legacy 8-bit Photoshop IRB text payloads.
enum class PhotoshopIrbStringCharset : uint8_t {
    Latin,
    Ascii,
};

/// Resource limits applied during IRB decode to bound hostile inputs.
struct PhotoshopIrbDecodeLimits final {
    uint32_t max_resources    = 1U << 16;
    uint64_t max_total_bytes  = 64ULL * 1024ULL * 1024ULL;
    uint32_t max_resource_len = 32U * 1024U * 1024U;
};

/// Decoder options for \ref decode_photoshop_irb.
struct PhotoshopIrbDecodeOptions final {
    bool decode_iptc_iim                     = true;
    bool decode_xmp_packet                   = true;
    bool decode_icc_profile                  = true;
    PhotoshopIrbStringCharset string_charset = PhotoshopIrbStringCharset::Latin;
    PhotoshopIrbDecodeLimits limits;
    IptcIimDecodeOptions iptc;
    XmpDecodeOptions xmp;
    IccDecodeOptions icc;
};

struct PhotoshopIrbDecodeResult final {
    PhotoshopIrbDecodeStatus status = PhotoshopIrbDecodeStatus::Ok;
    uint32_t resources_decoded      = 0;
    uint32_t entries_decoded        = 0;
    uint32_t iptc_entries_decoded   = 0;
    uint32_t xmp_entries_decoded    = 0;
    uint32_t icc_entries_decoded    = 0;
};

/**
 * \brief Decodes a Photoshop IRB stream and appends resources into \p store.
 *
 * Each resource becomes one \ref Entry with:
 * - \ref MetaKeyKind::PhotoshopIrb (resource id)
 * - \ref MetaValueKind::Bytes (raw resource payload)
 *
 * A bounded interpreted subset is additionally emitted as
 * \ref MetaKeyKind::PhotoshopIrbField entries for fixed-layout and
 * descriptor-header resources:
 * - ResolutionInfo (0x03ED)
 * - AlphaChannelsNames (0x03EE)
 * - DisplayInfo (0x03EF)
 * - PStringCaption (0x03F0)
 * - BorderInformation (0x03F1)
 * - BackgroundColor (0x03F2)
 * - VersionInfo (0x0421)
 * - PrintFlags (0x03F3)
 * - EffectiveBW (0x03FB)
 * - QuickMaskInfo (0x03FE)
 * - TargetLayerID (0x0400)
 * - LayersGroupInfo (0x0402)
 * - JPEG_Quality (0x0406)
 * - GridGuidesInfo (0x0408)
 * - PhotoshopBGRThumbnail / PhotoshopThumbnail header fields (0x0409/0x040C)
 * - CopyrightFlag (0x040A)
 * - URL (0x040B)
 * - GlobalAngle (0x040D)
 * - ColorSamplersResource / ColorSamplersResource2 headers and records
 *   (0x040E/0x0431)
 * - Watermark (0x0410)
 * - ICC_Untagged (0x0411)
 * - EffectsVisible (0x0412)
 * - IDsBaseValue (0x0414)
 * - UnicodeAlphaNames (0x0415)
 * - IndexedColorTableCount (0x0416)
 * - TransparentIndex (0x0417)
 * - GlobalAltitude (0x0419)
 * - SliceInfo (0x041A)
 * - WorkflowURL (0x041B)
 * - AlphaIdentifiers (0x041D)
 * - URL_List (0x041E)
 * - ICC_Profile byte count (0x040F)
 * - EXIFInfo byte count (0x0422)
 * - ExifInfo2 byte count (0x0423)
 * - XMP byte count (0x0424)
 * - IPTCDigest (0x0425)
 * - PrintScaleInfo (0x0426)
 * - PixelInfo / PixelAspectRatio (0x0428)
 * - Descriptor-backed resource headers for LayerComps, MeasurementScale,
 *   TimelineInfo, SheetDisclosure, OnionSkins, CountInfo, PrintInfo2,
 *   PrintStyle, PathSelectionState, and OriginPathInfo
 * - LayerSelectionIDs (0x042D)
 * - LayerGroupsEnabledID (0x0430)
 * - ChannelOptions (0x0435)
 * - PrintFlagsInfo (0x2710)
 * - ClippingPathName (0x0BB7)
 *
 * If enabled, embedded IPTC-IIM, XMP, and ICC payloads are additionally
 * decoded from resource ids 0x0404, 0x0424, and 0x040F into their regular
 * OpenMeta entry families. IPTC-IIM and XMP entries are marked as
 * \ref EntryFlags::Derived.
 */
PhotoshopIrbDecodeResult
decode_photoshop_irb(std::span<const std::byte> irb_bytes, MetaStore& store,
                     const PhotoshopIrbDecodeOptions& options
                     = PhotoshopIrbDecodeOptions {}) noexcept;

/**
 * \brief Estimates Photoshop IRB decode counts using the same limits/options.
 */
PhotoshopIrbDecodeResult
measure_photoshop_irb(std::span<const std::byte> irb_bytes,
                      const PhotoshopIrbDecodeOptions& options
                      = PhotoshopIrbDecodeOptions {}) noexcept;

std::string_view
photoshop_irb_resource_name(uint16_t resource_id) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
