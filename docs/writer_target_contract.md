# Writer Target Contract

This page defines the bounded preserve/replace contract for OpenMeta transfer
writers.

It is scoped to metadata transfer and metadata-only target edits. OpenMeta does
not claim full arbitrary metadata-editor parity, pixel transcoding, or
byte-for-byte preservation of rewritten metadata structures.

For generated XMP merge, precedence, sidecar output, and sidecar cleanup rules,
see [xmp_sync_policy.md](xmp_sync_policy.md).

## Common Rules

Managed metadata families are replaced only when OpenMeta has a prepared block
for that family or an explicit strip policy targets that family.

Unmanaged data is preserved as source ranges where the target edit path can
parse the container safely. If parsing finds a structure outside the bounded
contract, the edit fails with an unsupported or malformed result instead of
guessing.

Managed XMP writeback has three modes:
- `EmbeddedOnly`: generated XMP remains in the managed embedded carrier.
- `SidecarOnly`: generated embedded XMP blocks are suppressed and returned as
  sidecar output.
- `EmbeddedAndSidecar`: generated XMP is written to both the embedded carrier
  and sidecar output.

Destination embedded-XMP stripping is supported only for sidecar-only
writeback on JPEG, TIFF/DNG, PNG, WebP, JP2, JXL, and bounded BMFF targets.
Destination sidecar cleanup is supported only for embedded writeback.

Image-dependent metadata is target-owned. When source and target images may
differ in dimensions, channel count, sample type, compression, orientation,
colorspace, or strip/tile storage layout, host code must preserve or provide
target-correct image buffer specs from the actual output image. Use
`PrepareTransferRequest::target_image_spec` when the host knows target
dimensions, orientation, samples-per-pixel, bit depth, sample format,
photometric interpretation, planar configuration, compression, or EXIF color
space. OpenMeta filters source EXIF/XMP image-layout fields during prepared
transfer rather than copying stale source width/height, channel/layout,
color-space aliases, or source-local storage offsets into another file. ICC
transfer should be enabled only when the host knows that the source profile is
valid for the target pixel buffer; otherwise the host writer should keep or
inject the target profile. The Python binding exposes the same structure as
`openmeta.TransferTargetImageSpec`; the C++ and Python `metatransfer` command
wrappers expose matching `--target-*` flags for smoke tests and file-helper
integration checks.

When a host edits portable XMP orientation or dimensions before direct native
writing, `translate_xmp_image_geometry(...)` can project those values into
canonical TIFF/EXIF tags. It still requires the same target image spec and
fails if XMP disagrees with the actual output raster. Width and height are
stored-raster dimensions, not display-oriented dimensions after applying the
orientation transform.

For coarse transfer safety, use `TransferProfile::safety`. The default
`CompatibleFile` mode is for metadata repackage or recompression into a
compatible target and preserves source camera/color metadata after the
target-owned image-layout filter above. `RenderedImage` is for exports whose
pixels may have changed, including RAW-to-rendered outputs. It keeps general
descriptive metadata, time fields, GPS, IPTC, and portable XMP, but filters
source raw color calibration, profile/gain tables, raw digests/storage
identifiers, raw value curves, linearity limits, calibration curves,
curve-control points, linearization/crop/correction tags, vendor RAW
geometry/color/correction/private tags for Phase One/Leaf, Sony, Canon, Nikon,
Fujifilm, Pentax, Panasonic, Olympus, Kodak, Minolta, Sigma, Samsung, Ricoh,
Apple/Google computational capture fields, DJI/FLIR thermal processing fields,
Casio/Sanyo preview or face-geometry fields, KyoceraRaw WB fields, Reconyx
trail-camera processing/environment fields, HP/JVC/GE private placeholders,
Motorola source-rendering/sensor fields, Nintendo parallax/camera-info fields,
Microsoft stitch/panorama geometry fields, camera raw settings XMP, source ICC
profiles, MakerNotes, and non-C2PA JUMBF data. Host code should provide
target-correct ICC/profile data and image specs separately.

## Transfer Safety Matrix

The table below is the public coarse policy for automatic transfer. It is not a
privacy policy and it is not a replacement for application-specific metadata
controls. Hosts may still strip more metadata. For per-candidate preflight UI,
`metadata_concepts.h` exposes transfer hints such as `safe`, `source_bound`,
`rendered_unsafe`, and `requires_target_image_spec`;
`transfer_concept_diagnostics_from_store(...)` maps those hints to
keep/drop/requires-target-image-spec actions, severity tokens, and default
message text for a selected transfer mode. Descriptor-aware overloads accept
`MetadataRawDataDescriptor` so RAW-processing diagnostics can account for a
known stored-RAW or rendered source context. Candidates and diagnostics also
report independent `sensitivity`; `safe` means technically portable, not safe
to publish. Apply host consent/privacy policy to personal-contact,
person-identity, location, and legal-rights metadata.

When the host or decoder already knows that the source pixel buffer is rendered
rather than stored RAW samples, set
`PrepareTransferRequest::has_source_raw_data_descriptor` and pass
`source_raw_data_descriptor.encoding = MetadataRawDataEncoding::Rendered`.
Preparation then filters RAW-processing metadata even under
`CompatibleFile`; this prevents RAW calibration, curve, and camera-raw recipe
data from being carried into metadata that no longer applies to the stored
pixels.

When a decoder knows that a curve/LUT-like metadata entry only applies to
compressed RAW storage, set
`source_raw_data_descriptor.requires_compressed_raw_encoding = true`. Concept
diagnostics can then drop that curve for uncompressed or packed raw buffers
while still keeping non-curve RAW facts such as black level and raw-storage
layout applicable.

When a decoder knows that a curve/LUT-like metadata entry only applies to the
primary raw plane, set
`source_raw_data_descriptor.requires_primary_raw_plane = true` and provide
`has_plane_index` / `plane_index` for the raw buffer being checked. Concept
diagnostics then drop that curve for non-primary planes and keep it conditional
when the active plane is unknown.

| Metadata group | Examples | `CompatibleFile` | `RenderedImage` | Notes |
| --- | --- | --- | --- | --- |
| General descriptive metadata | title, description, creator, artist, copyright, rating, label, keywords | Keep | Keep | Safe because it describes the asset or authorship rather than the source pixel encoding. |
| Capture time | `DateTimeOriginal`, `CreateDate`, subsecond fields, timezone offset fields, GPS time | Keep | Keep | If the output represents a new capture or synthetic composition, host policy should replace or strip these values. |
| Camera and lens acquisition facts | `Make`, `Model`, `LensModel`, exposure time, f-number, ISO, focal length, flash, metering mode | Keep | Keep | These are safe as capture facts. They are not treated as processing instructions. |
| Location and IPTC editorial fields | EXIF GPS, XMP GPS aliases, IPTC location, caption, byline, credit, rights, job/reference fields | Keep | Keep | Privacy-sensitive data is intentionally left to host policy. |
| Portable XMP | Dublin Core, XMP Rights, IPTC Extension, generated EXIF/IPTC projections | Keep after image-layout filtering | Keep after image-layout and rendered-safety filtering | XMP properties from raw-processing namespaces are handled separately below. |
| Target image layout and storage | width, height, orientation, samples per pixel, bits per sample, sample format, photometric interpretation, compression, rows/strips/tiles, offsets, byte counts, thumbnail/interchange offsets | Target-owned; source values filtered | Target-owned; source values filtered | Host code must preserve target values or provide `target_image_spec`. |
| Output color/profile metadata | ICC profile blocks, EXIF/XMP `ColorSpace`, color-space aliases, Photoshop ICC profile name | Keep only when the source profile is valid for the target pixel buffer | Drop source ICC/profile facts; host writes target profile | Rendered exports often need a new output profile such as sRGB, Display P3, or a host-managed working/output profile. |
| RAW/DNG sensor and color pipeline | CFA pattern, black/white levels, linearization tables, linear response limits, RAW value curves, RAW curve control points, RAW calibration curves, `ColorMatrix*`, `ForwardMatrix*`, `CameraCalibration*`, `AsShotNeutral`, DNG private/profile tags, DNG XMP profile properties (`dng:*`), profile/gain tables, raw digests/storage identifiers, opcode/correction lists, Phase One/Leaf `ColorMatrix1`, `ColorMatrix2`, `WB_RGBLevels`, sensor-calibration flat fields and linearization coefficients, Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/Samsung/Ricoh/Apple/Casio/Sanyo/KyoceraRaw/Reconyx/Motorola MakerNote color, HDR, source-rendering, and white-balance coefficient tables | Keep only for compatible RAW/DNG-style transfer; drop when the supplied source RAW data descriptor says pixels are rendered or the checked raw plane cannot use the metadata | Drop | These values describe how to turn original sensor data into rendered color. Reusing them on already-rendered pixels can make CMS or editors apply the raw transform twice. Curve/LUT metadata is also conditional: prepare can consume coarse rendered-vs-stored, compressed-only, and primary-plane-only descriptor hints now, while exact activity for a raw blob/compression mode still needs deeper descriptor binding. |
| RAW crop, geometry, storage, correction, and private data | `ActiveArea`, `DefaultCrop*`, masked areas, opcode lists, distortion/vignetting/camera-profile correction data, Phase One/Leaf `SensorWidth`, `SensorHeight`, `SensorLeftMargin`, `SensorTopMargin`, `ImageWidth`, `ImageHeight`, `CameraOrientation`, `RawFormat`, `RawData`, `StripOffsets`, `BlackLevel`, `BlackLevelData`, `SplitColumn`, sensor-temperature correction fields, Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/Samsung/Ricoh decoded RAW geometry/storage/lens-correction fields such as SR2/SRF, RAF header/directory/RAFData fields, MinoltaRaw PRD/RIF/WBG tables, Pentax lens-correction tables, Panasonic sensor subtables, Olympus image-processing/raw-development tables, Kodak sensor/black-level/raw-histogram fields, Samsung Type2 raw/color/correction fields, Ricoh sensor/crop/vignetting fields, Apple computational capture/HDR fields, Google HDR+ and shot-log fields, pixel-shift, multi-shot, composite, and auto-lighting optimizer fields, DJI thermal parameter tables, FLIR thermal raw-data/radiometric calibration/palette/PiP fields, Casio/Sanyo preview image, face geometry, and private data-dump fields, Reconyx trail-camera environment/trigger fields, HP/JVC/GE private placeholders, Motorola sensor fields, Nintendo parallax/camera-info fields, Microsoft stitch/panorama geometry fields, Canon crop/aspect/color-data tables, and Nikon NEF/distortion/vignette tables or named correction fields | Keep only for compatible RAW/DNG-style transfer; drop when the supplied source RAW data descriptor says pixels are rendered | Drop | These values are tied to the original sensor geometry, computational rendering, radiometric calibration, source preview data, or raw-processing pipeline. Vendor-private RAW/source tables are dropped in rendered mode even when individual fields are unknown, while named entries also receive narrower color/WB/geometry/correction buckets. Use `phaseone_raw_geometry_from_store()`, `phaseone_raw_processing_from_store()`, and `vendor_raw_processing_from_store()` only to interpret source RAW metadata; rendered exports need target-owned geometry and color/profile data. |
| Camera raw settings XMP | `crs:*` development settings and raw-edit recipe metadata | Keep; drop when the supplied source RAW data descriptor says pixels are rendered | Drop | A rendered file should not normally carry a source raw-edit recipe unless the host intentionally writes a sidecar/workflow record. |
| Opaque MakerNote payloads | vendor MakerNote blobs and private nested IFDs | `Keep` carries the original raw `ExifIFD:MakerNote` payload as unverified opaque bytes; `Drop` omits it; unavailable `Invalidate` and `Rewrite` fail closed to `Drop` | Drop | Decoded safe facts can still be carried through standard EXIF/XMP fields; decoded-only vendor sub-IFDs are not reconstructed. Opaque preservation does not relocate nested vendor offsets, repair checksums, or prove semantic readability after EXIF repacking. Use `makernote_transfer_audit_from_store(...)` for generic trust and `makernote_layout_transfer_audit_from_store(...)` for bounded layout evidence. The layout audit identifies Nikon type 1 notes as outer-TIFF-dependent, can validate standard embedded-TIFF offsets in canonical Nikon type 3 notes, and conservatively identifies plausible Canon source-dependent IFD notes when the EXIF camera make is Canon. Canon recognition explicitly reports an ambiguous source offset basis and does not make the note rewritable. The audit does not validate vendor-private binary offsets or checksums. Preserving an existing destination MakerNote during an unrelated target edit is safer than transferring a source note into a new EXIF layout because the destination's established offset relationships can remain intact. |
| C2PA and JUMBF | APP11/JUMBF boxes, C2PA manifests, assertions, signatures | Follow explicit JUMBF/C2PA policy | Drop non-C2PA JUMBF; invalidate C2PA by default if it would otherwise be kept | Pixel-changing exports need a new content binding and signature from the host or signer. |
| Embedded previews and thumbnails | TIFF preview pages, SubIFD previews, JPEG interchange thumbnail data | Preserve only within bounded target-owned rewrite rules | Target-owned; host should regenerate or preserve target previews | Source previews commonly describe the source pixels, not the rendered target. |

## Target Summary

| Target | Managed embedded carriers | Preserve/replace rule | Main limits |
| --- | --- | --- | --- |
| `JPEG` | APP1 EXIF, APP1 XMP, APP2 ICC, APP13 IPTC/IRB, bounded APP11 JUMBF/C2PA | Replace matching recognized leading metadata segments; preserve unknown leading segments and image scan data | Not a general JPEG marker editor |
| `TIFF` | root TIFF tags, EXIF/GPS/Interop IFDs, bounded page/SubIFD chains, XMP tag 700, IPTC 33723, ICC 34675 | Rewrite bounded IFD structures and append new metadata tail data; preserve unrelated root tags and bounded downstream tails | Not arbitrary nested-IFD graph rewrite |
| `DNG` | same as TIFF plus DNG target-mode policy and minimal `DNGVersion` synthesis | Uses the TIFF-family rewrite contract; preserves DNG core target tags when merging non-DNG source metadata into an existing DNG target | Not a full DNG-specific rewrite engine |
| `PNG` | `eXIf`, XMP `iTXt`, `iCCP` | Insert prepared chunks after `IHDR`; replace matching managed chunks; preserve unrelated chunks | Requires valid PNG with terminal `IEND` |
| `WebP` | `EXIF`, XMP RIFF chunk, `ICCP`, bounded `C2PA` | Replace matching managed RIFF chunks; preserve unrelated chunks; patch `VP8X` feature bits | EXIF/XMP/ICC edits require an existing `VP8X` chunk |
| `JP2` | top-level `Exif`, top-level XMP `xml` box, `jp2h/colr` ICC | Replace matching top-level metadata boxes; rewrite `jp2h` only to replace/insert `colr`; preserve unrelated boxes and unrelated `jp2h` children | Does not synthesize `jp2h`; requires one existing `jp2h` for ICC |
| `JXL` | top-level `Exif`, XMP `xml` box, `jumb`, `c2pa` | Replace matching top-level boxes; preserve signature and non-managed boxes; classify `jumb` as generic JUMBF or C2PA | ICC is encoder handoff only; file edit emits uncompressed prepared metadata boxes |
| `HEIF` / `AVIF` / `CR3` | BMFF metadata items (`Exif`, XMP `mime`, JUMBF/C2PA), bounded `colr/prof` ICC properties, plus OpenMeta-authored metadata-only `meta` boxes | Preserve non-`meta` top-level boxes; replace prior OpenMeta-authored metadata `meta`; merge/replace/strip item metadata in parseable foreign top-level `meta` graphs by extending `iinf`/`iloc`/`idat`/`iref`; remap unambiguous managed item replacements across `iref`/version-0 `grpl`/`ipma`; replace prior ICC `colr` properties and remap `iprp`/`ipco`/`ipma` for bounded ICC | Does not rewrite arbitrary BMFF scene/property graphs |
| `EXR` | safe string header attributes through the EXR transfer emitter or adapter batch | No file rewrite contract today; host applies prepared attributes through its own EXR writer | Attribute-emitter target, not a file edit path |

When any decoded IPTC dataset is dirty, JPEG APP13 and TIFF tag 33723 payloads
are rebuilt from the active decoded datasets. A preserved raw Photoshop IPTC
resource is used only while those datasets remain untouched, so translated or
edited native IPTC cannot be shadowed by stale raw bytes.

## JPEG

The JPEG edit path scans leading metadata marker segments before image scan
data.

OpenMeta replaces a leading segment when:
- the segment route matches a prepared block route
- sidecar-only writeback requests embedded XMP stripping and the segment is
  APP1 XMP
- JUMBF or C2PA policy resolves to removal for matching APP11 carriers

Unknown APP/COM segments and all scan data after the leading metadata region
are preserved. If same-size replacements are available, OpenMeta can use the
in-place edit plan; otherwise it performs a metadata rewrite while preserving
the image data range.

## TIFF And DNG

TIFF-family editing supports classic TIFF and BigTIFF.

Managed updates include:
- root IFD metadata tags such as XMP tag 700, IPTC tag 33723, and ICC tag
  34675
- EXIF payload materialized as bounded TIFF-family IFD structures
- EXIF/GPS/Interop pointer regeneration
- bounded preview-page chain replacement with downstream tail preservation
- bounded `SubIFD` replacement with downstream auxiliary-tail preservation
- preserving an existing target `InteropIFD` when a replaced `ExifIFD` omits
  its own interop child

The active root image IFD remains target-owned for pixel layout and local byte
storage. Source EXIF values for root image width/height, sample layout,
compression, orientation, strip/tile offsets, strip/tile byte counts, and
thumbnail/JPEG interchange offsets are not allowed to replace the target's
active image structure during metadata transfer. For replaced preview-page and
`SubIFD` structures, OpenMeta also strips source-local strip/tile/JPEG storage
offsets and preserves the corresponding target-local storage fields when a
matching target child exists.

Source IFD offsets are never copied as ordinary metadata values when OpenMeta
does not also materialize their child directory. This currently applies to
`GlobalParametersIFD` (`0x0190`) and DNG `ExtraCameraProfiles` (`0xC6F5`):
prepare omits the source pointer and reports it as an unsupported EXIF entry.
An existing target pointer and its target-owned child bytes remain unchanged
during TIFF/DNG rewrite. Fresh output therefore contains neither a dangling
source offset nor a partially reconstructed auxiliary directory.

The same target-owned rule is applied before packaging EXIF/XMP metadata for
JPEG, PNG, WebP, JP2, JXL, BMFF-family targets, and EXR string attributes.
Source descriptive tags such as make/model/date/GPS/copyright continue to
transfer; source image-buffer facts do not become target image facts unless a
host supplies target-correct values through `target_image_spec` or another
writer-specific path.

Unrelated root IFD tags are preserved. Rewritten structures may be repacked
into new offsets appended to the file; offset identity is not part of the
contract.

DNG uses the same TIFF-family rewrite contract. `ExistingTarget` and
`TemplateTarget` require a backing target container. `MinimalFreshScaffold`
can emit a minimal metadata scaffold without an existing DNG target. When a
non-DNG source is merged into an existing DNG target, OpenMeta preserves
existing target DNG core tags within the bounded DNG policy layer.

## PNG

The PNG edit path requires a valid PNG signature, an `IHDR` chunk, and a
terminal `IEND` chunk.

Prepared `eXIf`, XMP `iTXt`, and `iCCP` chunks are inserted immediately after
`IHDR`. Existing chunks from those managed families are removed when a
replacement or strip policy targets that family. XMP matching is limited to
`iTXt` chunks using the `XML:com.adobe.xmp` keyword.

All unrelated chunks are preserved. Bytes following the terminal `IEND` are
preserved as part of the kept terminal range.

## WebP

The WebP edit path requires a valid RIFF/WebP stream whose RIFF size consumes
the input bytes.

Prepared `EXIF`, XMP, `ICCP`, and bounded `C2PA` chunks replace existing
chunks from the same managed family. Unrelated chunks are preserved. When
EXIF, XMP, or ICC is present after rewrite, OpenMeta patches the existing
`VP8X` feature flags to match the final metadata state.

Prepared WebP `EXIF` chunks carry the TIFF byte stream directly. They do not
include the JPEG APP1 `Exif\0\0` preamble.

The current WebP edit contract requires an existing `VP8X` chunk for EXIF,
XMP, or ICC metadata edits. It does not synthesize `VP8X`.

## JP2

The JP2 edit path requires a valid JP2 signature box and file-type box.

Prepared top-level `Exif` and XMP `xml` boxes replace existing top-level boxes
from the same managed family. Unrelated top-level boxes are preserved.

ICC update uses the bounded `jp2h/colr` route. OpenMeta rewrites the existing
`jp2h` box to replace existing `colr` children with the prepared ICC `colr`
payload, while preserving unrelated `jp2h` children. If no `colr` child exists,
the prepared `colr` child is inserted. The contract requires one existing
`jp2h` box and does not synthesize a new `jp2h` box.

## JXL

The JXL edit path requires a valid JXL container.

Prepared top-level `Exif`, XMP `xml`, `jumb`, and `c2pa` boxes replace existing
boxes from the same managed family. Unrelated top-level boxes are preserved,
including the JXL signature box. Generic JUMBF and C2PA are distinguished so a
generic `jumb` replacement does not accidentally remove a C2PA carrier, and
the C2PA path does not remove unrelated generic JUMBF.

When Brotli support is available, the same family classification can inspect
compressed `brob` metadata boxes by real type. The file edit path emits
prepared metadata boxes directly; the `jxl:icc-profile` route remains an
encoder-side handoff and is not serialized by the file edit path.

## HEIF / AVIF / CR3

The bounded BMFF edit path preserves non-`meta` top-level boxes as source
ranges.

When the target has no foreign top-level `meta` box, OpenMeta writes one
metadata-only top-level `meta` box using the public bounded contract. That box
can contain prepared Exif, XMP, JUMBF, C2PA, and ICC `colr` property payloads.
A prior OpenMeta-authored metadata `meta` box is removed and replaced by the
newly prepared box.

When the target already has a foreign top-level `meta` box, OpenMeta does not
append a second competing `meta` graph. For parseable HEIF/AVIF-style item
graphs it merges, replaces, or strips bounded Exif/XMP/JUMBF/C2PA metadata
items in the existing `meta` by extending `iinf`, `iloc`, `idat`, and `iref`
with `cdsc` references to the primary item. This constrained merge requires a
single parseable `iinf`, `iloc` version 0/1/2, `pitm`, and at most one `idat`.
When inserted metadata needs an item ID wider than 16 bits, OpenMeta upgrades
the rebuilt `iloc` to version 2, emits `infe` version 3 for those inserted
items, and uses `iref` version 1 for the corresponding `cdsc` references. If
the existing graph has exhausted the usable 32-bit item-id space or mixes
item-table shapes outside this bounded contract, the edit fails instead of
truncating IDs.

Newly inserted metadata item payloads are appended to the rebuilt `idat`
payload. To preserve broad reader compatibility, their `iloc` records keep
construction method 0 and use absolute file-offset extents. Compact valid
graphs with an omitted offset field or offset/length fields too narrow for the
inserted payloads are normalized to explicit 32-bit fields. Omitted extent
lengths are rejected because their source-to-end semantics cannot be preserved
safely when the graph grows. When all retained self-contained item locations
can be represented as absolute extents, OpenMeta compacts the rebuilt `iloc`
base-offset field width to zero for simpler reader compatibility.

Retained foreign item locations are supported when they use construction method
0 with file offsets, or construction method 1 with offsets into an existing
`idat`, with data reference index 0 or an explicitly parsed self-contained
version-0 `dinf`/`dref` `url ` or `urn ` entry. Construction method 2 is
supported only when the retained item has parseable `iref` `iloc` references,
using explicit extent indexes or reference order, and every referenced item is
also retained with a supported local location and data reference index 0.
Non-self-contained data references, missing method-2 references, removed
referenced items, and other construction methods fail as unsupported instead
of being rewritten by guesswork.

When exactly one removed managed item and one inserted item belong to the same
Exif, XMP, JUMBF, or C2PA family, OpenMeta remaps that item ID in retained
`iref` relation endpoints, version-0 `grpl` item-group members, and `ipma`
associations. A strip operation has no replacement, and multiple old or new
items make the mapping ambiguous; in both cases references to removed item IDs
are deleted rather than redirected by guesswork. Unrelated relations, groups,
properties, and associations remain unchanged.

For bounded ICC transfer, OpenMeta removes prior ICC `colr/prof` and
`colr/rICC` properties from `iprp/ipco`, compacts/remaps existing `ipma`
associations, appends the transferred `colr/prof` property, and associates it
with the primary item and any retained item that previously referenced one of
the replaced ICC properties. If the replaced association was marked essential,
the transferred ICC association keeps that bit. Arbitrary non-ICC property
replacement and broader BMFF scene/property graph rewriting remain out of
scope.

If sidecar-only writeback asks to strip embedded XMP, OpenMeta can remove XMP
from its own OpenMeta-authored metadata `meta` box and from parseable foreign
top-level `meta` item graphs that satisfy the same bounded primary-item
contract. Foreign graphs without `pitm`, with unsupported `iloc`, with multiple
competing item tables, or otherwise outside that bounded shape are reported as
unsupported instead of being guessed.

## EXR

EXR is a transfer target and host-emitter path, not a file rewrite path.

OpenMeta prepares safe flattened string attributes for transfer and exposes
the EXR attribute batch helpers for host-owned EXR writers. Existing EXR file
metadata preservation is therefore owned by the host writer, not by an
OpenMeta file edit helper.

One important host use case is late-bound EXR metadata. Streaming tile or
scanline writers sometimes only know a value after pixel data is finished,
for example `total_compute_time`. The safe fast design is a fixed-size
reservation/patch contract: reserve a typed attribute, such as a `double`,
before writer construction, then patch exactly those value bytes after close.
That operation remains owned by the host EXR writer for the current roadmap.
OpenMeta supplies the prepared attribute value but does not patch an existing
EXR file; variable-length header changes could otherwise require moving later
file structures.

## Non-Goals

The writer contract does not promise:
- arbitrary metadata editing for every carrier a format can contain
- preserving byte offsets of rewritten TIFF-family structures
- rewriting arbitrary existing BMFF item/property graphs
- synthesizing missing JP2 `jp2h` or WebP `VP8X` structures
- file-level EXR metadata rewrite
- signed C2PA rewrite or trust-policy parity beyond the bounded staged
  handoff paths
