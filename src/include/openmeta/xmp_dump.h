// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/api.h"

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

/**
 * \file xmp_dump.h
 * \brief XMP sidecar generation for a decoded \ref MetaStore.
 */

OPENMETA_PUBLIC_BEGIN
namespace openmeta {

/// Conflict policy for existing XMP versus generated portable XMP properties.
///
/// This currently applies to portable XMP generation only.
enum class XmpConflictPolicy : uint8_t {
    /// Preserve the historical OpenMeta order:
    /// EXIF-derived properties first, then existing XMP, then IPTC-derived
    /// properties.
    CurrentBehavior,
    /// Existing decoded XMP properties win over generated EXIF/IPTC mappings.
    ExistingWins,
    /// Generated EXIF/IPTC mappings win over existing decoded XMP properties.
    GeneratedWins,
};

/// Existing XMP namespace policy for portable XMP generation.
enum class XmpExistingNamespacePolicy : uint8_t {
    /// Keep only the standard portable namespaces known to OpenMeta.
    KnownPortableOnly,
    /// Also preserve safe simple/indexed properties from custom namespaces.
    PreserveCustom,
};

/// Existing standard portable-namespace reconciliation policy.
enum class XmpExistingStandardNamespacePolicy : uint8_t {
    /// Preserve existing standard portable namespaces subject to conflict order.
    PreserveAll,
    /// Drop OpenMeta-managed standard portable properties and regenerate them
    /// canonically from EXIF/IPTC mappings when available.
    CanonicalizeManaged,
};

/// XMP dump result status.
enum class XmpDumpStatus : uint8_t {
    Ok,
    /// Output buffer was too small; \ref XmpDumpResult::needed reports required size.
    OutputTruncated,
    /// Caller-specified limits prevented generating a complete dump.
    LimitExceeded,
};

/// Resource limits applied during dump to bound output generation.
struct XmpDumpLimits final {
    /// If non-zero, refuse to generate output larger than this many bytes.
    uint64_t max_output_bytes = 0;
    /// If non-zero, refuse to emit more than this many entries.
    uint32_t max_entries = 0;
};

/// Dump options for \ref dump_xmp_lossless.
struct XmpDumpOptions final {
    XmpDumpLimits limits;
    bool include_origin = true;
    /// Includes wire family/code/count and EXR-specific wire type name when available.
    bool include_wire  = true;
    bool include_flags = true;
    bool include_names = true;
};

/// Options for \ref dump_xmp_portable.
struct XmpPortableOptions final {
    XmpDumpLimits limits;
    /// Include TIFF/EXIF/GPS derived properties.
    bool include_exif = true;
    /// Include IPTC-IIM derived portable XMP properties.
    bool include_iptc = true;
    /// Include \ref MetaKeyKind::XmpProperty entries already present in the store.
    ///
    /// \note Currently simple `property_path` values, indexed `[n]` paths,
    /// bounded lang-alt paths like `title[@xml:lang=x-default]`, bounded
    /// one-level structured paths like `CreatorContactInfo/CiEmailWork`,
    /// bounded qualified one-level structured child paths like
    /// `LocationShown[1]/xmp:Identifier[1]`,
    /// `LocationShown[1]/exif:GPSLatitude`,
    /// `DerivedFrom/stRef:documentID`,
    /// `JobRef[1]/stJob:id`,
    /// `RenditionOf/stRef:filePath`,
    /// `Ingredients[1]/stRef:documentID`,
    /// `MaxPageSize/stDim:w`,
    /// `Fonts[1]/stFnt:fontName`,
    /// `Fonts[1]/stFnt:childFontFiles[1]`,
    /// `Colorants[1]/xmpG:swatchName`,
    /// `SwatchGroups[1]/xmpG:groupName`,
    /// `ProjectRef/path`,
    /// `beatSpliceParams/riseInTimeDuration/scale`,
    /// `markers/cuePointParams/key`,
    /// `contributedMedia[1]/duration/scale`,
    /// `Tracks[1]/trackName`,
    /// `Tracks[1]/markers/name`,
    /// `Tracks[1]/markers/cuePointParams/key`,
    /// `resampleParams/quality`,
    /// `startTimecode/timeValue`,
    /// `timeScaleParams/quality`,
    /// `Pantry[1]/InstanceID`,
    /// `Pantry[1]/dc:format`,
    /// `videoFrameSize/stDim:w`,
    /// `videoAlphaPremultipleColor/xmpG:mode`,
    /// `Manifest[1]/stMfs:reference/stRef:filePath`, and
    /// `Versions[1]/stVer:event/stEvt:action`,
    /// bounded indexed-structured paths like `Licensee[1]/LicenseeName`,
    /// bounded second-level structured scalar paths like
    /// `CreatorContactInfo/CiAdrRegion/ProvinceName`, bounded structured
    /// child indexed paths like `CreatorContactInfo/CiAdrExtadr[1]`, and
    /// bounded structured child lang-alt paths like
    /// `CreatorContactInfo/CiAdrCity[@xml:lang=x-default]` are emitted.
    /// Known standard malformed flat structured child values like
    /// `Creator[1]/Name` and `Creator[1]/Role` are also promoted into the
    /// canonical `lang-alt` or indexed shapes when no explicit canonical
    /// child entries already exist.
    /// Known malformed Adobe structured child paths like
    /// `DerivedFrom/documentID`, `JobRef[1]/id`,
    /// `Manifest[1]/reference/filePath`, and `Versions[1]/event/action`
    /// are likewise promoted into their canonical qualified child prefixes.
    /// The same bounded promotion applies to known second-level standard
    /// shapes like `CreatorContactInfo/CiAdrRegion/ProvinceName` and
    /// `CreatorContactInfo/CiAdrRegion/ProvinceCode`.
    /// Bounded second-level structured child lang-alt paths like
    /// `CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=x-default]`
    /// and bounded second-level structured child indexed paths like
    /// `CreatorContactInfo/CiAdrRegion/ProvinceCode[1]` are also emitted.
    bool include_existing_xmp = false;
    /// Existing XMP namespace writeback policy for portable output.
    XmpExistingNamespacePolicy existing_namespace_policy
        = XmpExistingNamespacePolicy::KnownPortableOnly;
    /// Existing standard portable-namespace reconciliation policy.
    XmpExistingStandardNamespacePolicy existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::PreserveAll;
    /// Conflict policy between existing decoded XMP and generated portable
    /// EXIF/IPTC mappings.
    XmpConflictPolicy conflict_policy = XmpConflictPolicy::CurrentBehavior;
    /// Emit `exif:GPSDateTime` instead of `exif:GPSTimeStamp` for GPS time.
    ///
    /// Default keeps standard portable naming. This compatibility mode is
    /// useful for tools that normalize XMP GPS time under `GPSDateTime`.
    bool exiftool_gpsdatetime_alias = false;
};

/// Sidecar format selection for \ref dump_xmp_sidecar.
enum class XmpSidecarFormat : uint8_t {
    Lossless,
    Portable,
};

/// High-level sidecar options for \ref dump_xmp_sidecar.
struct XmpSidecarOptions final {
    XmpSidecarFormat format = XmpSidecarFormat::Lossless;
    XmpDumpOptions lossless;
    XmpPortableOptions portable;
    /// Initial output buffer size before automatic growth (0 uses default).
    uint64_t initial_output_bytes = 0;
};

/// Stable flat request for sidecar export.
///
/// This shape is intended for wrapper/front-end APIs that need one option set
/// independent of selected format.
struct XmpSidecarRequest final {
    XmpSidecarFormat format = XmpSidecarFormat::Lossless;
    XmpDumpLimits limits;

    /// Portable mode options (applied when format == Portable).
    bool include_exif                        = true;
    bool include_iptc                        = true;
    bool include_existing_xmp                = false;
    XmpExistingNamespacePolicy portable_existing_namespace_policy
        = XmpExistingNamespacePolicy::KnownPortableOnly;
    XmpExistingStandardNamespacePolicy portable_existing_standard_namespace_policy
        = XmpExistingStandardNamespacePolicy::PreserveAll;
    XmpConflictPolicy portable_conflict_policy
        = XmpConflictPolicy::CurrentBehavior;
    bool portable_exiftool_gpsdatetime_alias = false;

    /// Lossless mode options (applied when format == Lossless).
    bool include_origin = true;
    bool include_wire   = true;
    bool include_flags  = true;
    bool include_names  = true;

    /// Initial output buffer size before automatic growth (0 uses default).
    uint64_t initial_output_bytes = 0;
};

/// Dump result (size stats + how many entries were emitted).
struct XmpDumpResult final {
    XmpDumpStatus status = XmpDumpStatus::Ok;
    uint64_t written     = 0;
    uint64_t needed      = 0;
    uint32_t entries     = 0;
};

/**
 * \brief Emits a lossless OpenMeta dump as a valid XMP RDF/XML packet.
 *
 * The output is safe-by-default:
 * - Text fields are XML-escaped and additionally restricted to a safe ASCII subset.
 * - Binary payloads (bytes/text/arrays/scalars) are stored as base64.
 *
 * This dump is intended as a storage-agnostic sidecar format for debugging and
 * offline workflows. It uses a private namespace (`urn:openmeta:dump:1.0`) and
 * is not meant to replace standard, interoperable XMP mappings.
 */
XmpDumpResult
dump_xmp_lossless(const MetaStore& store, std::span<std::byte> out,
                  const XmpDumpOptions& options) noexcept;

/**
 * \brief Emits a portable XMP sidecar packet (standard XMP schemas).
 *
 * The output is safe-by-default:
 * - XML reserved characters are escaped.
 * - Invalid control bytes are emitted as deterministic ASCII escapes.
 *
 * This mode is intended for interoperability (e.g. XMP sidecars alongside RAW/JPEG files).
 * It emits a best-effort mapping from decoded EXIF/TIFF/GPS/IPTC-IIM fields
 * to standard XMP properties (e.g. `tiff:Make`, `exif:ExposureTime`,
 * `xmp:ModifyDate`, `xmp:CreateDate`, `exif:GPSLatitude`, `dc:subject`, `photoshop:City`,
 * `Iptc4xmpCore:Location`).
 */
XmpDumpResult
dump_xmp_portable(const MetaStore& store, std::span<std::byte> out,
                  const XmpPortableOptions& options) noexcept;

/**
 * \brief Emits an XMP sidecar into a resizable byte buffer.
 *
 * This is a high-level wrapper around \ref dump_xmp_lossless and
 * \ref dump_xmp_portable that:
 * - selects format via \ref XmpSidecarOptions::format,
 * - grows output buffer automatically on \ref XmpDumpStatus::OutputTruncated,
 * - returns final bytes in \p out (trimmed to \ref XmpDumpResult::written on success).
 */
XmpDumpResult
dump_xmp_sidecar(const MetaStore& store, std::vector<std::byte>* out,
                 const XmpSidecarOptions& options) noexcept;

/**
 * \brief Converts \ref XmpSidecarRequest into \ref XmpSidecarOptions.
 *
 * Useful for wrappers and adapters that expose one flattened option model.
 */
XmpSidecarOptions
make_xmp_sidecar_options(const XmpSidecarRequest& request) noexcept;

/**
 * \brief Emits an XMP sidecar using the stable \ref XmpSidecarRequest model.
 */
XmpDumpResult
dump_xmp_sidecar(const MetaStore& store, std::vector<std::byte>* out,
                 const XmpSidecarRequest& request) noexcept;

}  // namespace openmeta
OPENMETA_PUBLIC_END
