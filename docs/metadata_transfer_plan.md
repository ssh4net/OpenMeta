# Metadata Transfer Plan (Draft)

Date: March 5, 2026

Related:
- [quick_start.md](quick_start.md)
- [host_integration.md](host_integration.md)
- [metadata_backend_matrix.md](metadata_backend_matrix.md)

## Goal

The transfer core is for metadata-only workflows:
- source: camera RAW and other supported inputs
- target: export-oriented container metadata
- scope: prepare once, then emit or edit many times

Pixel transcoding is out of scope.

## Design Rule

Transfer should not bottleneck high-throughput pipelines.

The core rule is:
- do the expensive work once during `prepare`
- reuse prebuilt metadata payloads during `compile` and `emit`
- keep per-frame work limited to optional time patching plus final container
  write calls

## Current Status

Current planning estimate for this lane: about `80-85%`.

Source-side readiness is already strong:
- tracked EXIF read gates are green on `HEIC/HEIF`, `CR3`, and mixed RAW corpora
- tracked MakerNote gates are green
- portable XMP gates are green
- EXR header interop gates are green

The main remaining work is now on the target side.

The first public write-side sync controls are also in place:
- generated XMP can explicitly suppress EXIF-derived projection
- generated XMP can explicitly suppress IPTC-derived projection
- `TransferProfile::safety` can distinguish compatible metadata
  repackage/recompression from rendered-image export, so callers can select a
  safe coarse policy without hand-picking individual tags
- prepared bundles record those resolved projection decisions alongside the
  existing preservation policies

Logical metadata edits are covered through preparation and typed backend
execution for every public target: JPEG, TIFF/DNG, JXL, WebP, PNG, JP2, HEIF,
AVIF, CR3, and EXR. The regression verifies an edited safe camera descriptor
reaches every target, while title, creator, and keyword edits reach every
XMP-capable target and replaced or removed values are not serialized again.
TIFF/DNG and EXR execution intentionally uses their typed writer or adapter
contracts rather than a generic byte stream. EXR does not embed XMP, so its
safe string-attribute projection is intentionally narrower than the
XMP-capable targets.

## Target Status Matrix

| Target | Status | Current shape | Main limits |
| --- | --- | --- | --- |
| JPEG | First-class | Prepared bundle, compiled emit, byte-writer emit, edit planning/apply, file helper, bounded JUMBF/C2PA staging | Not a full arbitrary metadata editor yet |
| TIFF | First-class | Prepared bundle, compiled emit, classic-TIFF and BigTIFF edit planning/apply, bounded preview-page chain rewrite (`ifd1`, `ifd2`, and preserved downstream tails), bounded SubIFD rewrite with preserved downstream auxiliary tails and preserved trailing existing children when only the front subset is replaced, bounded `ExifIFD -> InteropIFD` preservation when a replaced ExifIFD omits its own interop child, file helper, streaming edit path | Broader TIFF rewrite coverage is still narrower than JPEG |
| DNG | First-class | Dedicated public target layered on the TIFF backend; prepared bundle, compiled emit, minimal `DNGVersion` synthesis when missing, bounded DNG preview/aux merge policy, read-backed file-helper roundtrip, and the same bounded `SubIFD`/preview-tail/interop preservation contract as TIFF | Not a full DNG-specific rewrite engine or broad DNG policy surface |
| PNG | Bounded but real | Prepared bundle, compiled emit, bounded chunk rewrite/edit, file-helper roundtrip | Not a general PNG chunk editor |
| WebP | Bounded but real | Prepared bundle, compiled emit, bounded chunk rewrite/edit, file-helper roundtrip | Not a general WebP chunk editor |
| JP2 | Bounded but real | Prepared bundle, compiled emit, bounded box rewrite/edit, file-helper roundtrip | `jp2h` synthesis is still out of scope |
| JXL | Bounded but real | Prepared bundle, compiled emit, bounded box rewrite/edit, file-helper roundtrip | Still narrower than JPEG/TIFF |
| HEIF / AVIF / CR3 | Bounded but real | Prepared bundle, compiled emit, direct prepared item/property payload byte-writer handoff, OpenMeta-managed BMFF item/property edit, constrained foreign-`meta` item merge/replacement/strip, managed item-ID remapping across `iref`/version-0 `grpl`/`ipma`, plus bounded ICC property merge, file-helper roundtrip | Direct payload output is a host handoff rather than a standalone BMFF file; arbitrary foreign `meta` scene/property-graph rewrite is still unsupported |
| EXR | Bounded but real | Prepared bundle, compiled emit, direct backend attribute emit, prepared-bundle to `ExrAdapterBatch` bridge, CLI/Python transfer surface | No file rewrite/edit path yet; current transfer payload is safe string attributes only |

## What Is Already Implemented

### Core API

The shared transfer core already provides:
- `prepare_metadata_for_target(...)`
- `prepare_metadata_for_target_file(...)`
- `compile_prepared_transfer_execution(...)`
- `execute_prepared_transfer(...)`
- `execute_prepared_transfer_compiled(...)`
- `execute_prepared_transfer_file(...)`

This supports both:
- `prepare -> compile -> emit`
- `prepare -> compile -> patch -> emit/edit`

### Core Utility Layers

These support the public transfer flow:
- `TransferByteWriter`
- `SpanTransferByteWriter`
- prepared payload and package batch persistence
- adapter views for host integrations
- explicit time-patch support for fixed-width EXIF date/time fields
- transfer-policy decisions for MakerNote, JUMBF, C2PA, EXIF-to-XMP
  projection, and IPTC-to-XMP projection

### Current File-Helper Regression Coverage

OpenMeta now has explicit end-to-end read-backed transfer tests for:
- source JPEG -> JPEG edit/apply -> read-back
- source JPEG -> TIFF edit/apply -> read-back
- source JPEG -> DNG edit/apply -> read-back
- source JPEG -> TIFF edit/apply with bounded preview-page chain ->
  read-back
- source TIFF/BigTIFF with existing multi-page preview chain ->
  replace the front preview pages and preserve downstream tails
- source DNG-like TIFF with `subifd0` + `ifd1` -> TIFF edit/apply -> read-back
- source DNG-like TIFF with `subifd0` + `ifd1` -> BigTIFF edit/apply -> read-back
- source DNG-like TIFF with `subifd0` + `ifd1` -> DNG edit/apply -> read-back
- source TIFF/BigTIFF with existing `subifd0 -> next` auxiliary chain ->
  replace `subifd0` -> preserve downstream auxiliary tail
- source JPEG -> PNG edit/apply -> read-back
- source JPEG -> WebP edit/apply -> read-back
- source JPEG -> JP2 edit/apply -> read-back
- source JPEG -> JXL edit/apply -> read-back
- source JPEG -> bounded HEIF edit/apply -> read-back
- source JPEG -> bounded AVIF edit/apply -> read-back
- source JPEG -> bounded CR3 edit/apply -> read-back
- source ICC -> bounded BMFF ICC property edit/apply -> read-back and
  external validation when local HEIF/AVIF/CR3 encoders can create usable
  targets
- direct BMFF package planning/writing for prepared metadata items and bounded
  ICC `colr/prof` property payloads, high-level byte-writer handoff with
  capacity preflight and item/property summaries, plus owned package replay for
  constrained foreign-`meta` graph merges
- source XMP -> bounded BMFF XMP item edit/apply -> read-back and external
  validation on configured HEIF/AVIF/CR3 targets where local tools expose the
  transferred XMP payload
- source EXIF -> bounded BMFF Exif item edit/apply -> explicit ExifTool
  reader-layout regression check on configured HEIF/AVIF/CR3 targets

That does not make all targets equally mature, but it does mean the transfer
core has real roundtrip regression gates across the primary supported export
families.

The primary writer family is also covered by a deterministic compatibility-dump
gate for JPEG, TIFF, DNG, PNG, WebP, JP2, JXL, and bounded HEIF, AVIF, and
CR3 item metadata edits. That gate
checks the prepared EXIF/XMP routes, edit/apply status, dual-write XMP
writeback summary, and decoded metadata dump for the managed source fields.
Additional compatibility-dump gates cover sidecar-only writeback with explicit
sidecar-base overrides and embedded-only writeback with destination-sidecar
cleanup through persisted output.

There is now also a named in-tree transfer release gate:
- `openmeta_gate_transfer_release`
- `openmeta_transfer_release_gate`

In a non-Python test tree it runs:
- `MetadataTransferApi.*`
- `XmpDump.*`
- `ExrAdapter.*`
- `DngSdkAdapter.*`
- `openmeta_cli_metatransfer_smoke`

In a Python-enabled test tree it also runs:
- `openmeta_python_transfer_probe_smoke`
- `openmeta_python_metatransfer_edit_smoke`

The external image-usability gate can use optional configured HEIF, AVIF, and
CR3 target files via CMake cache paths when local tools cannot create those
formats. Configured BMFF targets exercise ICC property, XMP item, MakerNote,
and EXIF image-property transfer/read-back routes. For real configured targets,
the gate infers target-owned image dimensions, channel count, bit depth, sample
format, and photometric layout before transfer, so OpenMeta does not write the
synthetic fixture geometry into the destination metadata. ExifTool is used when
available for BMFF EXIF/XMP/ICC reader compatibility checks.

## Per-Target Notes

### JPEG

Strongest current target.

Implemented:
- EXIF as APP1
- XMP as APP1
- ICC as APP2
- IPTC as APP13
- edit planning and apply
- byte-writer emit
- bounded JUMBF/C2PA staging

### TIFF

Also a first-class target.

Implemented:
- EXIF, XMP, ICC, and IPTC transfer
- edit planning and apply
- classic-TIFF and BigTIFF rewrite support
- bounded `ifd1` chain rewrite support, including preserving an existing
  downstream page tail when `ifd1` is replaced
- bounded TIFF/DNG-style SubIFD rewrite support, including preserving an
  existing downstream auxiliary tail when `subifdN` is replaced
- bounded front-subset `SubIFD` replacement that preserves trailing existing
  children from the target file
- bounded `ExifIFD` replacement that preserves an existing target
  `InteropIFD` when the source replacement omits its own interop child
- target-owned root image layout and target-local TIFF storage pointers:
  source EXIF cannot replace the active target root image width/height,
  sample layout, compression, orientation, strip/tile offsets, strip/tile byte
  counts, or JPEG interchange offsets; replaced preview/SubIFD structures also
  drop source-local storage offsets and preserve matching target-local storage
  fields where available
- target-owned image-dependent metadata filtering across prepared transfer:
  EXIF/XMP image dimensions, channel/layout fields, source-local storage
  offsets, color-space aliases, and similar source image-buffer facts are
  omitted before packaging metadata for JPEG, TIFF/DNG, PNG, WebP, JP2, JXL,
  BMFF-family targets, and EXR string attributes. Host writers that change
  pixels must provide target image buffer specs and target-correct values
  instead of relying on copied source image properties. C++ callers can set
  `PrepareTransferRequest::target_image_spec` to inject target dimensions,
  orientation, samples-per-pixel, bit depth, sample format, photometric
  interpretation, planar configuration, compression, and EXIF color space after
  source image-layout fields have been filtered. The Python binding and both
  command-line transfer wrappers now expose the same target image spec surface
  for file-helper integration tests.
- rendered-output safety mode: `TransferProfile::safety =
  TransferSafetyMode::RenderedImage` additionally drops source raw color
  calibration, profile/gain tables, raw digests/storage identifiers,
  linearization/crop/correction metadata, vendor RAW
  geometry/color/correction/thermal/computational/private/stitch fields,
  camera raw settings XMP, source ICC profiles, MakerNotes, and non-C2PA JUMBF
  data. It is intended for RAW-to-rendered or otherwise pixel-changing exports
  where host code must provide target-correct color/profile data.
- concept-level BMFF diagnostics treat `tiled_image.configuration` as
  source-container-bound: compatible-file mode can keep the evidence, while
  rendered-image mode reports `drop.tiled_image_configuration`. This diagnostic
  does not authorize copying source `tilC` bytes into a rewritten destination
  item graph.
- safety regression coverage now checks both sides of that boundary:
  compatible-file mode keeps serializable source RAW/camera-specific metadata,
  while rendered-image mode drops the same source-specific data and injects
  host-provided target dimensions, channel count, bit depth, sample format,
  photometric interpretation, and orientation.
- current writer limits still apply after safety filtering: some compatible
  source metadata can be retained in the decoded `MetaStore` and audit, but
  may not yet serialize through every target writer path. Adobe DNG XMP
  properties (`dng:*`) are now emitted by the portable XMP writer when retained
  by compatible-file safety. The remaining writer gap is mainly reconstructed
  vendor-private MakerNote sub-IFDs and unknown/custom XMP namespaces that do
  not have a declared portable writer contract. OpenMeta preserves the raw
  `ExifIFD:MakerNote` payload when it is present, but decoded-only vendor
  MakerNote sub-IFD fields are reported as non-serializable and are not used to
  synthesize a new MakerNote blob.
- bounded DNG-style merge policy in the file-helper path:
  source-supplied preview/aux front structures replace the target front
  structures, while existing target page tails and trailing auxiliary
  children are preserved
- file-based helper flow
- streaming edit output

### DNG

Implemented as a dedicated public target on top of the TIFF backend.

Implemented:
- EXIF, XMP, ICC, and IPTC transfer through the TIFF-family backend
- read-backed file-helper roundtrip for JPEG-source and DNG-like-source input
- minimal `DNGVersion` synthesis when the source metadata lacks it
- explicit public target modes:
  - `ExistingTarget`
  - `TemplateTarget`
  - `MinimalFreshScaffold`
- bounded preview-page chain rewrite/merge
- bounded raw-image `SubIFD` rewrite/merge
- preservation of existing target DNG core tags when a non-DNG source is
  merged into an existing DNG target
- preserved downstream page tails and downstream auxiliary tails
- preserved trailing existing auxiliary children when only the front subset
  is replaced
- bounded `ExifIFD` replacement that preserves an existing target
  `InteropIFD` when the source replacement omits its own interop child

Current limits:
- still a bounded DNG policy layer, not a full DNG-specific rewrite engine
- broader arbitrary nested-IFD graph rewrite is still out of scope
- in the file-helper path, `ExistingTarget` and `TemplateTarget` now require
  an explicit target path; only `MinimalFreshScaffold` keeps the metadata-only
  prepare/emit path available without a backing DNG container

Optional host bridge:
- When OpenMeta is built with `OPENMETA_WITH_DNG_SDK_ADAPTER=ON` and a
  `dng_sdk` package is discoverable, `openmeta/dng_sdk_adapter.h` exposes a
  bounded Adobe DNG SDK bridge for:
  - applying prepared OpenMeta DNG bundles onto `dng_negative`
  - updating an existing `dng_stream` via the SDK's metadata-update path
  - a direct file-helper for `source file -> existing DNG file` in-place
    update
  - a matching direct Python binding over that file-helper
  - a thin CLI wrapper via `metatransfer --update-dng-sdk-file`
- This adapter is optional. Core `Dng` transfer support remains available
  without the Adobe SDK.
- The OpenMeta build must use a C++ runtime/standard library compatible with
  the discovered `dng_sdk` package.
- Public automated CI intentionally excludes this SDK-backed lane because the
  Adobe DNG SDK is not part of the public dependency/distribution story.
  Treat it as a maintainer or release-validation path rather than a default
  GitHub Actions requirement.

### PNG

Implemented as a bounded chunk target:
- `eXIf`
- XMP `iTXt`
- `iCCP`
- bounded rewrite/edit for managed metadata chunks

### WebP

Implemented as a bounded RIFF metadata target:
- `EXIF`
- `XMP `
- `ICCP`
- bounded `C2PA`
- bounded rewrite/edit for managed metadata chunks
- `EXIF` chunk payloads use direct TIFF bytes, without the JPEG APP1
  `Exif\0\0` preamble

### JP2

Implemented as a bounded box target:
- `Exif`
- `xml `
- bounded `jp2h` / `colr`
- bounded rewrite/edit for top-level managed metadata
- replacement of managed `colr` in an existing `jp2h`

### JXL

Implemented as a bounded box target:
- `Exif`
- `xml `
- bounded `jumb`
- bounded `c2pa`
- encoder ICC handoff
- bounded box-based edit path

### HEIF / AVIF / CR3

Implemented as a bounded BMFF target family:
- `bmff:item-exif`
- `bmff:item-xmp`
- bounded `bmff:item-jumb`
- bounded `bmff:item-c2pa`
- bounded `bmff:property-colr-icc`
- bounded OpenMeta-managed metadata-only `meta` rewrite path
- constrained foreign top-level `meta` item merge for parseable `iinf`,
  `iloc` version 0/1/2, `pitm`, optional single `idat`, and primary-item `cdsc`
  references
- bounded 32-bit item-id insertion for foreign item graphs, including
  automatic `iloc` version 2 upgrade when an otherwise supported `iloc`
  version 0/1 graph exhausts the 16-bit item-id space
- inserted metadata item records keep `iloc` construction method 0 and use
  absolute file-offset extents for broad reader compatibility
- retained foreign item locations support construction method 0 file offsets
  and construction method 1 `idat` extents with data reference index 0 or an
  explicitly self-contained version-0 `dinf`/`dref` `url ` or `urn ` entry
- retained construction method 2 item-reference extents are supported when
  `iref` `iloc` references are parseable by explicit extent index or reference
  order and referenced items are also retained with supported local locations;
  its referenced target records remain limited to data reference index 0;
  missing references, removed referenced items, non-self-contained data
  references, and other construction methods fail safely
- unambiguous one-old-to-one-new managed Exif, XMP, JUMBF, and C2PA item
  replacement remaps retained `iref` endpoints, version-0 `grpl` item-group
  members, and `ipma` item associations; strip and ambiguous replacement paths
  remove references to deleted managed item IDs instead of leaving them stale
- rebuilt foreign `iloc` graphs compact foldable self-contained base offsets to
  a zero-width base-offset field when safe
- bounded foreign top-level `meta` ICC property merge by replacing prior ICC
  `colr/prof` and `colr/rICC` properties, remapping `ipma`, and associating the
  transferred `colr/prof` property with the primary item and any retained item
  that previously referenced a replaced ICC property while preserving the prior
  essential association bit
- bounded foreign top-level `meta` XMP replacement and strip support for
  parseable item graphs that satisfy the same primary-item contract
- fail-safe rejection for unsupported foreign top-level `meta` shapes and
  broader BMFF item/property graph rewrite shapes outside the bounded contract

### EXR

Implemented today as a bounded first-class target:
- `prepare_metadata_for_target(...)`
- `prepare_metadata_for_target_file(...)`
- `compile_prepared_transfer_execution(...)`
- `emit_prepared_bundle_exr(...)`
- `emit_prepared_transfer_compiled(...)`
- public CLI/Python `--target-exr` transfer surface

It still keeps the older integration bridge:
- `build_exr_attribute_batch(...)`
- `build_exr_attribute_part_spans(...)`
- `build_exr_attribute_part_views(...)`
- `replay_exr_attribute_batch(...)`

The transfer lane now also exposes:
- `build_prepared_exr_attribute_batch(...)`
- `build_exr_attribute_batch_from_file(...)`
- Python `build_exr_attribute_batch_from_file(...)` for direct file-to-batch
  host-side inspection without going through the generic transfer probe
- Python helper wrappers:
  `openmeta.python.probe_exr_attribute_batch(...)` and
  `openmeta.python.get_exr_attribute_batch(...)`

That keeps EXR host integrations on the transfer path: callers can prepare one
`TransferTargetFormat::Exr` bundle, then materialize a native
`ExrAdapterBatch` without re-projecting from the source `MetaStore`.

Current EXR transfer scope is intentionally conservative:
- safe flattened `string` header attributes
- backend emission through `ExrTransferEmitter`
- no general file-based EXR metadata rewrite/edit path yet
- no typed EXR attribute synthesis beyond the current safe string projection

Important user use case to keep visible:
- A tile/region streaming EXR writer may know some metadata only after all
  pixel chunks are written, for example `total_compute_time`.
- The practical fast path for that use case is not a general variable-length
  header rewrite. It is a fixed-size reservation/patch contract: declare a
  fixed-width attribute, such as a `double`, before opening the EXR writer,
  write pixel chunks normally, then patch only the reserved value bytes after
  close.
- OpenMeta does not expose that late-bound EXR patch plan yet. If EXR depth is
  expanded, the first useful scope should be a bounded fixed-size patch API
  with offset discovery, type/size validation, and a decode-after-patch
  verification gate.

## Transfer Policies

The public transfer contract now models five policy subjects:
- MakerNote
- JUMBF
- C2PA
- XMP EXIF projection
- XMP IPTC projection

Each uses explicit `TransferPolicyAction` values:
- `Keep`
- `Drop`
- `Invalidate`
- `Rewrite`

Prepared bundles also record the resolved policy decisions and reasons so
callers do not have to infer behavior from warning text alone.

For the XMP projection subjects, the current public knobs are intentionally
simple:
- EXIF-derived properties can be mirrored into generated XMP or suppressed
- IPTC-derived properties can be mirrored into generated XMP or suppressed
- generated portable XMP can choose how existing decoded XMP conflicts with
  generated EXIF/IPTC mappings

This gives callers stable write-side control over the most important projection
behavior without forcing them to reverse-engineer the transfer output.

## Write-Side Sync Controls

OpenMeta now has a bounded public sync-policy layer for generated XMP.

Current controls:
- `xmp_project_exif`
- `xmp_project_iptc`
- `xmp_existing_namespace_policy`
  - `KnownPortableOnly`
  - `PreserveCustom`
- `xmp_conflict_policy`
- `xmp_existing_sidecar_mode` on the file-read/prepare path:
  - `Ignore`
  - `MergeIfPresent`
- `xmp_existing_sidecar_precedence` on the file-read/prepare path:
  - `SidecarWins`
  - `SourceWins`
- `xmp_existing_destination_embedded_mode` on the file-read/prepare and
  file-helper execution paths:
  - `Ignore`
  - `MergeIfPresent`
- `xmp_existing_destination_embedded_precedence` on the file-read/prepare and
  file-helper execution paths:
  - `DestinationWins`
  - `SourceWins`
- `xmp_writeback_mode` on the file-helper execution path:
  - `EmbeddedOnly`
  - `SidecarOnly`
  - `EmbeddedAndSidecar`
- `xmp_destination_embedded_mode` on the file-helper execution path:
  - `PreserveExisting`
  - `StripExisting`
- CLI:
  - `--xmp-include-existing-sidecar`
  - `--xmp-existing-sidecar-precedence <sidecar_wins|source_wins>`
  - `--xmp-include-existing-destination-embedded`
  - `--xmp-existing-destination-embedded-precedence <destination_wins|source_wins>`
  - `--xmp-no-exif-projection`
  - `--xmp-no-iptc-projection`
  - `--xmp-conflict-policy <current|existing_wins|generated_wins>`
  - `--xmp-writeback <embedded|sidecar|embedded_and_sidecar>`
  - `--xmp-destination-embedded <preserve_existing|strip_existing>`
  - `--xmp-destination-sidecar <preserve_existing|strip_existing>`

Current bounded writeback contract:

| `xmp_writeback_mode` | Edited file | Sibling `.xmp` | Default cleanup behavior |
| --- | --- | --- | --- |
| `EmbeddedOnly` | Keep generated XMP in the managed embedded carrier | No generated sidecar output | Preserve any existing sibling `.xmp` unless `xmp_destination_sidecar_mode=StripExisting` |
| `SidecarOnly` | Suppress generated embedded XMP carriers | Return generated sidecar output | Preserve existing embedded XMP unless `xmp_destination_embedded_mode=StripExisting` |
| `EmbeddedAndSidecar` | Keep generated XMP in the managed embedded carrier | Return the same generated XMP as sibling `.xmp` output | No sidecar cleanup path is requested |

Current behavior:
- existing XMP can still be included independently
- EXIF payload emission stays independent from EXIF-to-XMP projection
- IPTC native carrier emission stays independent from IPTC-to-XMP projection
- portable generated XMP can keep the historical mixed order, prefer existing
  decoded XMP, or prefer generated EXIF/IPTC mappings when the same portable
  property would collide
- existing sibling `.xmp` sidecars from the destination path can be merged
  into generated portable XMP before transfer packaging when explicitly
  requested
- that sidecar merge path now has explicit precedence against source-embedded
  existing XMP instead of relying on implicit decode order
- existing embedded XMP from the destination file can also be merged into
  generated portable XMP on the file-read/prepare path and on the file-helper
  path when explicitly requested
- that destination-embedded merge path has its own explicit precedence
  against source-embedded existing XMP instead of relying on implicit decode
  order
- some targets without a native IPTC carrier can still use XMP as the bounded
  fallback carrier when IPTC projection is enabled
- file-helper export can now strip prepared embedded XMP blocks and return
  canonical sidecar output guidance instead
- file-helper export can also keep generated embedded XMP while emitting the
  same generated packet as a sibling `.xmp` sidecar
- the public `metatransfer` CLI and Python transfer wrapper can now persist
  that generated XMP as a sibling `.xmp` sidecar when sidecar or dual-write
  XMP writeback is selected
- the public `metatransfer` CLI and Python transfer wrapper now also expose
  the bounded destination-embedded merge and precedence controls directly
- sidecar-only writeback now has an explicit destination embedded-XMP policy:
  - preserve existing embedded XMP by default
  - strip existing embedded XMP for `jpeg`, `tiff`, `png`, `webp`, `jp2`,
    and `jxl`
- embedded-only writeback now has an explicit destination sidecar policy:
  - preserve an existing sibling `.xmp` by default
  - strip an existing sibling `.xmp` when explicitly requested
- the C++ API now also has a bounded persistence helper for
  `execute_prepared_transfer_file(...)` results, so applications can write the
  edited file, write the generated `.xmp` sidecar, and remove a stale sibling
  `.xmp` without reimplementing wrapper-side file logic
- the Python binding now exposes the same persistence path through
  `transfer_file(...)` and `unsafe_transfer_file(...)`, and the public Python
  wrapper uses that core helper instead of maintaining its own sidecar write
  and cleanup implementation
- the Python binding now also exposes the reusable decoded-source path through
  `read_transfer_source_snapshot_file(...)`,
  `read_transfer_source_snapshot_bytes(...)`,
  `Document.build_transfer_source_snapshot()`, and
  `transfer_snapshot_probe(...)` / `transfer_snapshot_file(...)`
- public API regression coverage now asserts dual-write roundtrip and
  persistence behavior across `jpeg`, `tiff`, `dng`, `png`, `webp`, `jp2`,
  `jxl`, `heif`, `avif`, and `cr3`

This is deliberately narrower than a full sync engine. It does not yet define:
- full EXIF vs XMP precedence rules
- MWG-style reconciliation
- full destination embedded-vs-sidecar reconciliation policy beyond the
  current bounded merge, precedence, carrier-mode, and strip rules
- namespace-wide deduplication and normalization rules beyond the current
  generated-XMP path

## Time Patch Plan

Time patching is intentionally narrow and fixed-width.

Current model:
- build EXIF payloads once
- record patch slots in the bundle
- patch only the affected bytes during execution

Primary fields:
- `DateTime`
- `DateTimeOriginal`
- `DateTimeDigitized`
- `SubSecTime*`
- `OffsetTime*`

This is meant for fast repeated transfer, not general metadata editing.

## Main Blockers

### 1. General edit UX

OpenMeta still does not present one fully mature, general-purpose metadata
editor across all formats. The current transfer core is real, but still more
bounded than ExifTool or Exiv2.

### 2. Broader EXIF / IPTC / XMP sync policy

This remains one of the biggest product gaps for writer adoption, even though
the first public projection controls now exist.

Missing pieces include:
- conflict resolution rules
- broader sidecar vs embedded policy beyond the current bounded writeback mode
- canonical writeback policy
- broader namespace reconciliation behavior beyond the current bounded
  custom-namespace preservation control

### 3. MakerNote-safe rewrite expectations

Read parity is strong, but broad rewrite guarantees for vendor metadata are not
yet at the level of mature editing tools. The generic trust boundary is now
explicit: decoded-only fields are not serialized, `Keep` means unverified
opaque byte carry-forward, and unavailable `Rewrite` fails closed to `Drop`.
The generic public audit reports that nested-offset relocation, vendor checksum
repair, semantic validation, and raw-carrier passthrough are unavailable. The
first vendor-layout audit now distinguishes canonical Nikon type 1 notes, whose
offsets depend on the outer TIFF, from type 3 notes with an embedded TIFF at
byte 10. Bounded validation can prove that a type 3 embedded TIFF's standard
directory/value offsets remain inside the opaque payload; it does not prove
vendor-private binary offsets, checksum validity, or semantic readability.

Preserving a destination MakerNote while editing unrelated target metadata is a
different operation from moving a source MakerNote into newly serialized EXIF.
The former can retain the target's established layout; the latter may invalidate
outer-TIFF-relative offsets even when the `0x927C` payload bytes are unchanged.
Synthetic JPEG/TIFF transfer tests now prove byte-identical type 3 payload
preservation and selected Nikon field readability after repacking. Remaining
work is the broader vendor/version-specific offset-base and integrity inventory,
followed by rewrite lanes only where relocation rules are fully understood.

### 4. EXR depth

The architectural question is now how far to deepen the current bounded EXR
target:
- keep EXR as a backend-emitter target plus bridge helpers, or
- add a broader EXR file rewrite/edit path

## Recommended Next Priorities

1. Keep transfer ahead of further read-breadth work.
2. Stabilize the current target family:
   - JPEG
   - TIFF
   - PNG
   - WebP
   - JP2
   - JXL
   - bounded BMFF
3. Decide how deep EXR should go beyond the current bounded target.
4. Add more transfer-focused roundtrip and compare gates where they improve
   confidence for adopters.
5. Add an explicit EXIF / IPTC / XMP sync policy.

## Three-Phase Writer Confidence Roadmap

This is the public roadmap for closing the main read/transfer/write gaps
against mature metadata tools without expanding the project scope into full
pixel editing or unbounded arbitrary metadata editing.

### Working Rule

For the current milestone:
- do not add broad new read families before the current writer target family is
  stable
- every new writer behavior should ship with explicit roundtrip and compare
  gates
- prefer one documented bounded contract per target over format-specific hidden
  behavior

### Phase 1: Writer Confidence Baseline

Goal:
- restore a fully green public tree
- make current bounded writer behavior explicit and regression-gated
- remove adoption risk caused by ambiguous sync and preservation behavior

Cross-cutting deliverables:
- fix all public unit and regression failures before adding further writer
  breadth
- publish one explicit EXIF / IPTC / XMP writeback policy for:
  - source-embedded vs destination-embedded precedence
  - source-embedded vs destination-sidecar precedence
  - canonical generated-XMP writeback rules
  - namespace preservation and canonicalization rules
- add compare-backed release validation for the current primary target family
- document unmanaged-metadata preservation rules for each writer target

Exit criteria:
- public test tree is green
- public release gates cover read-back plus compare-style validation for the
  primary target family
- writer-side XMP behavior is explicit instead of implementation-defined

### Phase 2: Stable First-Class Writer Set

Goal:
- make the current target family feel consistent across C++, CLI, and Python
- raise bounded edit paths into stable first-class writer contracts
- improve rewrite safety for vendor metadata without claiming full arbitrary
  edit parity

Cross-cutting deliverables:
- one stable user-facing edit/transfer surface across C++, CLI, and Python
- one policy surface for MakerNote, JUMBF, C2PA, EXIF projection, IPTC
  projection, and XMP carrier behavior
- stronger rewrite guarantees for preserve-vs-replace behavior on existing
  target metadata
- compare and roundtrip gates promoted from smoke coverage into release-facing
  validation for the first-class target family

Exit criteria:
- the first-class target family has documented write guarantees and matching
  regression gates
- major host surfaces expose the same bounded policy controls
- target maturity differences are reduced to known documented limits instead of
  accidental behavior

### Phase 3: Deeper Parity and Read-Depth Follow-Through

Goal:
- close the most visible remaining competitor gaps after the primary writer
  contract is stable
- deepen read semantics only where they materially improve writer confidence or
  interop parity

Cross-cutting deliverables:
- deeper modern-container semantics where current bounded projections are too
  small for parity workflows
- long-tail read-depth work for formats that still rely mainly on embedded-TIFF
  follow paths
- broader compare gates for newer target families and long-tail metadata
  structures

Exit criteria:
- remaining gaps are mostly strategic out-of-scope items rather than missing
  baseline format behavior
- OpenMeta can defend a clear public answer for which targets are first-class,
  bounded, or read-only

### Per-Target Deliverables

| Target family | Phase 1 deliverable | Phase 2 deliverable | Phase 3 follow-up |
| --- | --- | --- | --- |
| JPEG | Lock explicit sync/writeback defaults for APP1/APP2/APP13, add compare-backed read-back gates for edit/apply, and document unmanaged-marker preservation | Promote JPEG from strongest target to reference writer contract for other backends, including clearer preserve/replace guarantees for existing metadata carriers | Expand parity coverage for more mixed metadata bundles and signed/unsigned JUMBF handoff workflows where still bounded |
| TIFF | Lock rewrite guarantees for root IFD, `ExifIFD`, preview-page chains, and bounded `SubIFD` replacement; add compare-backed roundtrip gates for classic TIFF and BigTIFF | Raise current bounded rewrite behavior into a stable first-class contract, including clearer preserve/replace rules for existing auxiliary chains and downstream tails | Broaden nested-IFD graph handling only where it improves practical export parity without claiming arbitrary graph rewrite |
| DNG | Lock explicit behavior for `ExistingTarget`, `TemplateTarget`, and `MinimalFreshScaffold`; add compare-backed roundtrip gates for `DNGVersion`, preview chains, and raw `SubIFD` merge behavior | Stabilize the DNG policy layer so hosts can rely on predictable preserve/merge behavior without the Adobe SDK path | Decide whether any additional DNG-specific rewrite depth is worth public contract expansion beyond the TIFF-derived bounded model |
| PNG | Lock chunk replacement rules for `eXIf`, XMP `iTXt`, and `iCCP`; add compare-backed roundtrip gates for embedded-only, sidecar-only, and dual-carrier XMP flows where applicable | Promote the current bounded chunk path into a stable managed-metadata editor with explicit preservation rules for unrelated chunks | Add deeper parity only if compare workflows show recurring gaps against major tools |
| WebP | Lock replacement rules for `EXIF`, `XMP `, `ICCP`, and bounded `C2PA`; add compare-backed roundtrip gates | Promote the current bounded RIFF metadata path into a stable managed-metadata editor with explicit preservation guarantees for unrelated chunks | Extend only where public parity data shows material gaps for common WebP metadata workflows |
| JP2 | Lock top-level managed-box rewrite rules and `colr` replacement behavior; add compare-backed roundtrip gates | Promote bounded box rewrite into a stable JP2 metadata contract with explicit preserve/replace guarantees for unmanaged boxes | Revisit `jp2h` synthesis only if it becomes necessary for common export parity |
| JXL | Lock current box rewrite rules for `Exif`, `xml `, `jumb`, bounded `c2pa`, and encoder-side ICC handoff; add compare-backed roundtrip gates for edit/apply paths | Promote JXL from bounded but real to a stable first-class metadata target with explicit unmanaged-box preservation rules and clearer encoder/file-edit split | Add more `brob` realtype coverage only after the current direct and bounded compressed routes are stable |
| HEIF / AVIF / CR3 | Lock the bounded metadata-only `meta` edit contract, item/property preservation rules, and compare-backed roundtrip gates | Stabilize the bounded BMFF writer contract for metadata items, ICC property handling, and existing OpenMeta-authored `meta` replacement | Deepen BMFF scene semantics and relation modeling where current bounded fields are too small for parity workflows |
| EXR | Decide whether the public target remains an attribute-emitter contract or grows into a file rewrite/edit path; add compare-backed gates for the chosen contract | If EXR stays bounded, make that contract final and explicit across C++, CLI, and Python; if it grows, define one narrow first-class rewrite scope and gate it | Add depth only after the architectural choice is settled; avoid half-bounded expansion |
| RAF / X3F | Keep RAF header-declared preview-JPEG EXIF/XMP discovery, FujiIFD/TIFF follow path, RAF header/directory geometry decode, RAFData geometry projection, standalone XMP fallback, rendered-transfer dropping of native RAF source fields, and X3F header/PROP/section-JPEG reads stable with focused compare coverage | Only add read depth that directly improves downstream transfer/export confidence | Deepen remaining RAF model-specific tables and X3F image-processing sections if parity evidence shows real user-facing gaps |
| CRW / CIFF | Keep the current bounded native CIFF projection stable and well-gated | Improve only the parts that materially affect interop or transfer workflows | Revisit deeper native legacy coverage if it becomes a recurring parity blocker |
| Photoshop IRB | Keep raw preservation stable and add compare coverage for the current interpreted subset | Expand interpreted subset only where it improves practical writer parity | Revisit broader Photoshop-resource parity after the first-class writer set is stable |
| JUMBF / C2PA | Keep bounded preserve/invalidate behavior deterministic and regression-gated | Improve public signer-handoff and bounded rewrite workflows without claiming full trust-policy parity | Revisit deeper semantics and signed rewrite coverage after the main writer contract is stable |

### Phase Ordering Summary

1. Fix public regressions and lock the sync/writeback contract.
2. Turn the current target family into a stable first-class writer set.
3. Spend follow-up effort on deeper parity lanes only after the writer
   baseline is trustworthy.

## Competitor Parity Checklist

This section is a practical tracking view for the remaining gap against
general-purpose metadata competitors.

Estimated remaining work packages:
- to reach normal still-image workflow parity close to `Exiv2`: about `8`
  major work packages
- to reach broader overall parity closer to `ExifTool`: about `12-15`
  major work packages

These are not release-percentage numbers. They are rough planning counts for
distinct parity workstreams that still matter after the current public writer
contract work.

### Package Status

Status legend:
- `Done`: public contract and regression coverage are good enough that this is
  no longer a primary parity blocker
- `Partial`: real support exists, but competitor-visible limits still remain
- `Missing`: still a clear parity gap

| Work package | Why it matters for parity | Status | Remaining package count | Main target families |
| --- | --- | --- | --- | --- |
| Public writer contract for primary targets | Competitors feel predictable on preserve/replace behavior; OpenMeta still needs that same trust level across all first-class targets | `Partial` | `1` | `TIFF`, `DNG`, `PNG`, `WebP`, `JP2`, `JXL`, `HEIF/AVIF/CR3` |
| General EXIF / IPTC / XMP sync engine | One of the biggest remaining gaps for general editing adoption | `Partial` | `1-2` | Cross-cutting |
| Compare-backed release validation | Needed to defend parity claims with repeatable read-back and compare gates | `Partial` | `1` | Cross-cutting |
| MakerNote rewrite trust | Generic preserve/rewrite/drop trust is explicit; Nikon type 1/type 3 layout evidence and type 3 JPEG/TIFF preservation regressions are present, but vendor-private relocation, checksum repair, and semantic guarantees still trail mature tools | `Partial` | `1` | `JPEG`, `TIFF`, `DNG`, RAW-derived lanes |
| TIFF / DNG deeper rewrite guarantees | Important for serious export/edit trust on camera-originated files | `Partial` | `1` | `TIFF`, `DNG` |
| BMFF writer depth beyond current bounded contract | Needed for stronger `HEIF/AVIF/CR3` parity beyond the current metadata-only `meta` model | `Partial` | `1` | `HEIF`, `AVIF`, `CR3` |
| Modern container read-depth follow-through | Remaining visible read gaps are mostly here | `Partial` | `1` | `HEIF/AVIF`, `JXL` |
| Long-tail native format semantics | Matters more against `ExifTool` than against `Exiv2` | `Partial` | `2-3` | `RAF`, `X3F`, `CRW/CIFF`, `Photoshop IRB` |
| EXR target decision | Current EXR target is real but still architecturally bounded | `Partial` | `1` | `EXR` |
| JUMBF / C2PA deeper semantics | Current support is bounded and useful, but not full trust-policy parity | `Partial` | `1-2` | `JPEG`, `PNG`, `WebP`, `JXL`, `BMFF` |
| Full arbitrary metadata editing parity | Mature competitors expose a broader open-ended editor surface | `Missing` | Strategic / out of scope | Cross-cutting |

### Format-Family Gap Map

This map is intentionally coarse. It answers where the main remaining work
still sits after the current public regression and writer-contract work.

| Format family | Read parity | Transfer/write parity | Main remaining competitor gap |
| --- | --- | --- | --- |
| `JPEG` | `Strong` | `Strong` | needs continued compare-backed hardening and deeper mixed-bundle parity, not a new baseline writer |
| `TIFF` | `Strong` | `Partial` | deeper rewrite guarantees for more existing-graph and tail-preservation cases |
| `DNG` | `Strong` | `Partial` | more predictable preserve/merge behavior across target modes and raw `SubIFD` chains |
| `PNG` | `Strong` | `Partial` | stable unmanaged-chunk preservation contract and broader compare-backed validation |
| `WebP` | `Strong` | `Partial` | stable unrelated-chunk preservation contract and broader compare-backed validation |
| `JP2` | `Strong` | `Partial` | stronger managed-box preservation guarantees and more roundtrip validation |
| `JXL` | `Strong` on current lanes | `Partial` | more explicit box-preservation guarantees and deeper `brob` realtype follow-through |
| `HEIF / AVIF / CR3` | `Strong` on tracked lanes | `Partial` | BMFF writer depth and deeper scene/relation semantics beyond the bounded current model |
| `EXR` | `Bounded but real` | `Bounded but real` | still needs an explicit long-term decision between stable bounded target vs rewrite/edit path |
| `RAF / X3F` | `Partial` | not a main writer lane | deeper RAF model-specific native tables and X3F image-processing sections beyond current carrier/header/property lanes |
| `CRW / CIFF` | `Partial` | bounded | legacy native depth still trails mature tools |
| `Photoshop IRB` | `Partial` | bounded preservation | interpreted subset still smaller than mature tools |
| `JUMBF / C2PA` | `Partial` | bounded | deeper semantics, trust-policy behavior, and signed rewrite parity remain out of scope |

### Practical Readout

If OpenMeta stops after the current writer-contract work, it can already argue
that it is close to competitor parity on the main tracked still-image targets.

The latest read-gap closure added RAF preview-JPEG EXIF/XMP discovery before
the native FujiIFD/raw section and kept native RAF fields in the rendered-image
safety drop set. This closes a common competitor-visible RAF read gap without
making RAF a rendered-output writer lane.

To get materially closer to `Exiv2`, the remaining work is mostly:
- finish stable writer guarantees for the first-class target family
- finish the broader sync policy
- harden compare-backed release validation
- improve TIFF/DNG/BMFF rewrite trust

To get materially closer to `ExifTool`, OpenMeta also needs:
- more long-tail native format depth
- broader general editing behavior
- deeper `JUMBF/C2PA` semantics
- a clearer answer for `EXR`

### Execution Order

Use this as the practical delivery order for the remaining parity work.

Priority legend:
- `Now`: should be in the next active delivery slice
- `Next`: should start after the `Now` slice is stable
- `Later`: important for broader parity, but not the next blocker

| Work package | Priority | Why this order |
| --- | --- | --- |
| Public writer contract for primary targets | `Now` | This is the core trust gap that still keeps OpenMeta below mature writer parity |
| General EXIF / IPTC / XMP sync engine | `Now` | This is still one of the biggest adoption blockers for general edit workflows |
| Compare-backed release validation | `Now` | Parity claims remain weaker until compare-backed gates are release-facing instead of mostly API-facing |
| TIFF / DNG deeper rewrite guarantees | `Now` | This is the highest-risk writer lane for serious still-image export confidence |
| BMFF writer depth beyond current bounded contract | `Next` | `HEIF/AVIF/CR3` are already real targets, but the bounded writer model still needs more depth for stronger parity |
| MakerNote rewrite trust | `In progress` | Generic behavior fails closed; the Nikon layout inventory has started with type 1/type 3 evidence, while vendor-private relocation and integrity lanes remain |
| Modern container read-depth follow-through | `Next` | Visible gap, but less urgent than finishing the current writer baseline |
| EXR target decision | `Next` | Needs an explicit product choice, but should follow the main writer-contract stabilization work |
| RAW curve/data applicability model | `In progress` | Coarse descriptor-backed rendered-source filtering is now available in prepare; precise RAW interpretation still needs curve/LUT metadata tied to the actual raw data storage path before it is called active |
| Long-tail native format semantics | `Later` | Matters more for broad `ExifTool` parity than for the first still-image writer milestone |
| JUMBF / C2PA deeper semantics | `Later` | Current bounded behavior is already useful; deeper trust semantics should wait until the core writer contract is stable |
| Full arbitrary metadata editing parity | `Later` | Strategic follow-up, not part of the next parity-closing milestone |

Suggested delivery sequence:
1. Finish the stable writer contract for the first-class target family.
2. Finish the broader sync-policy layer and compare-backed release validation.
3. Harden the two highest-risk writer lanes: `TIFF/DNG` and bounded `BMFF`.
4. Continue the Nikon inventory beyond standard embedded-TIFF offsets, then
   apply the same vendor/version-specific offset and integrity evidence pattern
   to Canon, Olympus, and Sony; enable rewrite only for fully verified lanes.
5. Spend follow-up time on modern-container depth, `EXR`, and long-tail native semantics only after the main writer baseline is defendable.

### Now Slice Implementation Board

This board turns the current `Now` slice into concrete delivery checklists.

The sync item here means the bounded next-slice policy completion needed for
practical writer parity. It does not mean full arbitrary EXIF/IPTC/XMP sync
parity across every workflow.

#### 1. Public Writer Contract For Primary Targets

- [x] document final preserve-vs-replace behavior for existing embedded XMP on `TIFF`, `DNG`, `PNG`, `WebP`, `JP2`, `JXL`, and bounded `BMFF`
- [x] document final preserve-vs-replace behavior for destination sidecars across embedded-only, sidecar-only, and dual-write flows
- [x] lock explicit unmanaged-metadata preservation rules for unrelated chunks, boxes, items, and tails per target family
- [x] add compare-backed read-back gates for each first-class target instead of relying mainly on API-shape regression coverage
- [x] make CLI and Python surfaces describe the same writeback behavior and path-derivation rules as the C++ helper
- [x] reduce remaining target differences to documented limits instead of accidental implementation details

Evidence: `docs/writer_target_contract.md` defines per-target preserve/replace
rules and the remaining bounded limits. The BMFF section now makes the item-id
width rule explicit: supported foreign `iloc` version 0/1 graphs can be
upgraded to output `iloc` version 2 when inserted metadata needs 32-bit item
IDs, while unsupported graph shapes or exhausted 32-bit ID space still fail
safely.

#### 2. Bounded EXIF / IPTC / XMP Sync Layer

- [x] publish one final precedence table for source embedded XMP, destination embedded XMP, and destination sidecar XMP
- [x] lock conflict behavior for generated EXIF-to-XMP and IPTC-to-XMP projections when existing XMP is also present
- [x] lock canonical generated-XMP writeback behavior for embedded-only, sidecar-only, and dual-write flows
- [x] lock namespace preservation and canonicalization rules for managed vs unmanaged XMP content
- [x] add regression cases for mixed embedded-plus-sidecar destination states across the primary target family
- [x] document the explicit non-goals of this bounded sync layer so it is not confused with full arbitrary sync parity

Evidence: `docs/xmp_sync_policy.md` now defines the bounded public policy,
including carrier precedence, generated-vs-existing conflict behavior,
writeback modes, namespace handling, and non-goals. The release-facing transfer
tests cover source/destination carrier precedence, generated EXIF/IPTC versus
existing XMP conflicts, canonical managed namespace replacement, and persisted
embedded/sidecar writeback across the primary writer target family.

#### 3. Compare-Backed Release Validation

- [x] promote the current primary-target roundtrip checks into explicit release-facing compare gates
- [x] add compare-backed validation for `TIFF`, `DNG`, `PNG`, `WebP`, `JP2`, `JXL`, and bounded `BMFF` target outputs, including fail-safe rejection for unsupported foreign top-level `meta` BMFF shapes
- [x] cover embedded-only, sidecar-only, and dual-write XMP flows in release-facing compare validation
- [x] add compare-backed validation for explicit sidecar-base overrides and destination-sidecar cleanup behavior
- [x] gate the primary writer family on deterministic read-back of managed metadata after edit/apply
- [x] keep public parity claims tied to compare-backed evidence instead of only unit or smoke coverage

Evidence: this plan keeps parity language at the supported-lane level and ties
release claims to `openmeta_gate_transfer_release`, compatibility-dump
read-back, and image-usability gates. BMFF high-item-ID insertion is covered by
a release-gated API roundtrip that writes into an `iloc` v2 graph and scans the
result back as one Exif item and one XMP item.

#### 4. TIFF / DNG Deeper Rewrite Guarantees

- [x] lock rewrite guarantees for classic TIFF and BigTIFF root IFD, `ExifIFD`, preview chains, and bounded `SubIFD` replacement
- [x] lock explicit DNG behavior for `ExistingTarget`, `TemplateTarget`, and `MinimalFreshScaffold`
- [x] add compare-backed roundtrip gates for preview chains, raw `SubIFD` merge behavior, and `DNGVersion` persistence
- [x] document preserve-vs-replace guarantees for existing auxiliary IFD chains and downstream tails
- [x] harden read-back and rewrite tests around mixed existing metadata carriers on camera-like TIFF/DNG files
- [x] define the bounded edge of TIFF/DNG rewrite depth clearly enough that hosts know what is guaranteed and what is not

Evidence: `docs/writer_target_contract.md` defines the bounded TIFF/DNG
rewrite contract and DNG target-mode behavior. Release-facing
compatibility-dump tests now cover classic TIFF, DNG, and BigTIFF DNG-style
merge outputs with preview-tail preservation, `SubIFD` auxiliary-tail
preservation, existing root/preview/`SubIFD` XMP carrier replacement,
embedded-versus-sidecar precedence, and `DNGVersion` read-back.

#### Done-When Readout

- [x] the first-class target family has one explicit public writer contract
- [x] the bounded sync-policy layer is documented and regression-gated
- [x] release-facing compare validation covers the main still-image writer set
- [x] `TIFF/DNG` rewrite guarantees are strong enough to stop being a primary parity blocker
- [x] the next work slice can move to bounded `BMFF` depth instead of still backfilling the writer baseline

### Host Integration And Adoption Backlog

This backlog captures adoption-oriented work that supports host projects with
flat metadata models and deferred output writes. It should not replace the
writer-confidence slice above; it should be sequenced around it.

#### Near-Term Host Contract Work

- [x] add a small runtime capability query API for read, structured decode,
  transfer preparation, and target edit support by format and metadata family
- [x] mark public host-facing APIs with stability levels such as stable,
  experimental, or internal; start with `visit_metadata(...)`, snapshot
  read/build, fileless execution, and bundle execution
- [x] publish the generic `FlatHost` mapping contract: name style, duplicate
  handling, type projection, deterministic ordering, and namespace behavior
- [x] add a deterministic compatibility dump for names, values, scalar types,
  origins, and transfer/writeback decisions so downstream tests can avoid
  binary-packet baselines
- [x] document final conflict and precedence decisions for generated EXIF/XMP,
  IPTC/XMP, source embedded XMP, destination embedded XMP, and destination
  sidecar XMP

#### Medium-Term Fidelity Work

- [x] add an opt-in raw-preserving `TransferSourceSnapshot` mode that keeps
  original carrier provenance and bounded payload bytes alongside decoded
  `MetaStore` state
- [x] preserve bounded per-carrier provenance in raw snapshots: container type,
  block kind, byte range, original order, and route identity
- [x] connect decoded entries back to the raw carrier records they were derived
  from, using snapshot-local decoded entry ids
- [x] define policy choices for raw passthrough vs decoded re-emission when the
  destination container can safely accept either form
- [x] add a diagnostic raw-carrier passthrough audit that reports candidate
  carriers and primary block reasons before any raw passthrough writer path is
  enabled
- [x] add the first opt-in snapshot raw passthrough writer path for eligible
  non-C2PA JUMBF and draft unsigned C2PA invalidation carriers
- [x] provide versioned, bounded target-neutral snapshot serialization after
  settling the first raw/provenance model; preserve store identity, duplicate
  order, typed values, origins, flags, carriers, and decoded-entry links
- [x] provide transactional typed `FlatHost` import by exact source identity,
  unique exported name, or explicit `MetaKeyView`; reject ambiguous name-only
  inverse mapping
- [x] add FlatHost remove-by-identity and remove-by-unique-name operations that
  preserve stable `Dirty | Deleted` tombstones and raw-carrier entry links
- [x] document and test the deferred reconciliation sequence: deserialize
  snapshot, import changed/add/remove FlatHost records, retain untouched
  metadata, then prepare target payloads
- [x] compatibility-lock the canonical snapshot v1 bytes with exact decode and
  re-encode coverage plus atomic rejection of unknown versions
- [x] define the typed adapter operation schema as a dedicated v1 contract,
  validate every kind-specific field against a canonical rebuild, and expose
  EXR name/type/value without route parsing
- [ ] provide full prepared-bundle serialization only if a concrete host needs
  more than the existing snapshot and prepared payload/package persistence

#### Supporting Work

- [x] improve structured diagnostics with severity, stable code, carrier/family,
  offset or byte range where available, and short host-facing messages
- [ ] extend resource accounting beyond current hard limits with preflight
  estimates for prepared transfers, sidecar output, and serialized snapshots
- [x] add clean-room public micro fixtures for host integration tests; do not
  vendor large binary assets, third-party source drops, or scraped/spec text
- [ ] add examples for `read bytes -> snapshot -> target bytes -> edited bytes`
  and `visit_metadata(...) -> flat host attribute list`
- [x] add the allocation-free bounded random-access source foundation with
  exact reads, short-read/I/O/source-change results, and caller-owned request
  and byte accounting; format support still requires the scanner and decoder
  work below and must not silently materialize the complete source
- [x] keep OpenMeta C++20-only; C++17 consumers own any dedicated private
  wrapper, and no C++17 bridge is planned in this repository

#### Random-Access Read Work Packages

The host contract is a source size plus exact positional reads. It must be
implementable by file handles, network/range-backed storage, and host I/O
proxies without an OpenMeta dependency on any one host library.

1. [complete in 0.4.100] Define a borrowed
   `size + read_at(offset, destination)` source contract with
   exact-read, short-read, overflow, request-count, byte-budget, and source-size
   consistency failures. Concurrent calls against an immutable source must not
   share mutable decoder state.
2. [partial in 0.4.106] Refactor TIFF/DNG and TIFF-based camera RAW header/IFD
   traversal first. Classic TIFF, BigTIFF, DNG, RW2, and ORF structural/value
   decoding now uses caller-owned windows after type/count/range validation.
   PrintIM, GeoTIFF, Pentax DNG private data, selected self-contained
   MakerNotes, Nikon embedded TIFF/type 1, Sony outer-TIFF-relative IFDs, and
   contained Canon adjusted-base payloads are converted. Olympus outer-relative
   and modern nested IFDs, Panasonic binary tables, and Samsung STMN/Type2
   derived tables are also converted. Fujifilm self-relative and General
   Imaging Type 2 source layouts are converted. Kodak fixed-layout records and
   outer-TIFF-relative Type 8, Type 10, and Type 11 IFDs plus their vendor
   subtables are converted. Mixed-base Ricoh, Nintendo, Casio, Minolta, and FLIR
   paths are converted; Canon external derived subtables and unknown vendor
   layouts stay explicit residuals.
3. [complete in 0.4.109] Refactor shallow sequential container scanners and
   payload extraction for JPEG, PNG, WebP, GIF, and EXR, then bounded box
   traversal for BMFF, JP2, and JXL. JPEG, PNG, WebP, JP2, JXL, and ISO-BMFF
   scanning now has callback/span descriptor parity, caller-owned read windows,
   explicit source failures, and no image/media-payload reads. BMFF parity
   includes brands, item tables/references, ICC properties, and nested CR3
   metadata wrappers. GIF extension scanning, EXR header traversal, bounded
   logical-payload fetching, and reusable decoded snapshot assembly are also
   positional. Snapshot assembly aggregates source limits across phases and
   reports unconverted enrichment paths as explicit residuals.
4. [complete in 0.4.112] Refactor native RAF, X3F, and CRW paths and declared
   embedded-container recursion. Native RAF header/directories, X3F
   header/section-directory/PROP, and CRW/CIFF directory/value traversal are
   positional. RAF preview-JPEG/FujiIFD and X3F section-JPEG enrichment now
   follows declared ranges without reading JPEG entropy or RAW image payloads.
   Undeclared source-wide compatibility searches remain bounded residual work.
5. Gate each format on byte-span versus random-reader compatibility dumps,
   malformed/short-read tests, request/byte ceilings, and real-corpus parity
   before advertising that format as random-access capable. JPEG, PNG, WebP,
   GIF, JP2, JXL, ISO-BMFF, TIFF/DNG, and EXR positional decode/snapshot paths,
   malformed/I/O behavior, payload-skip checks, and cumulative request/byte
   ceilings are covered through 0.4.109; native RAF/X3F/CRW parity, denied
   image-payload ranges, malformed offsets, scratch sizing, and cumulative
   request ceilings are covered in 0.4.111. RAF preview/FujiIFD and X3F
   section-JPEG callback/span discovery parity, nonzero range offsets,
   entropy-denied reads, decode assembly, malformed ranges, and request limits
   are covered in 0.4.112. Undeclared source-wide searches and selected
   source-wide BMFF enrichment remain explicit work.

#### Raw Snapshot Emission Policy

Raw carriers are preserved for provenance, diagnostics, and opt-in bounded
passthrough decisions. Transfer preparation still defaults to decoded
re-emission from `MetaStore`.

`raw_carrier_passthrough_audit_from_snapshot(...)` is the current diagnostic
bridge between preserved carriers and emission policy. It reports
whether each preserved carrier is a passthrough candidate for a target format,
or blocked by missing payload bytes, target carrier incompatibility, active
safety filtering, content-bound C2PA, explicit profile policy, missing decoded
entry links, or unsupported carrier kind. The audit itself does not change
bundle preparation; it is a preflight tool for host UI and test coverage.

The planned policy choices are:
- `DecodedReemit`: current behavior; OpenMeta decodes entries, applies safety
  filtering, and emits freshly prepared EXIF/XMP/ICC/IPTC/JUMBF carriers.
- `RawPassthroughWhenSafe`: opt-in behavior through
  `TransferRawCarrierPassthroughMode::WhenSafe`; OpenMeta may reuse a preserved
  raw carrier only when the target accepts the same carrier family, the payload
  was preserved in the snapshot, and the active transfer safety/profile does
  not require filtering, invalidation, or target-owned image-property rewrite.
  The current writer path is intentionally narrow: non-C2PA JUMBF and
  OpenMeta draft unsigned C2PA invalidation carriers can be reused for JPEG,
  JXL, and BMFF targets, and draft unsigned C2PA invalidation carriers can be
  reused for WebP. EXIF/XMP/ICC/IPTC still use decoded re-emission.
- `HostOwnedPassthrough`: current integration escape hatch; hosts may inspect
  `TransferSourceSnapshot::raw_carriers` and implement their own passthrough
  outside OpenMeta's writer path.

Rendered-image transfer keeps using decoded re-emission plus safety filtering.
Opaque MakerNotes, content-bound C2PA, source ICC profiles, raw color
calibration, and target-owned image properties are not raw-passthrough
candidates for rendered exports.

## Postponed Work

Still out of scope for the current milestone:
- full arbitrary metadata editing parity
- full C2PA signed rewrite / trust-policy parity
- full EXIF / IPTC / XMP sync engine
- broad TIFF/DNG and BMFF rewrite parity beyond the bounded current targets
- mandatory raw-passthrough snapshots or snapshot serialization in the default
  read path
- C ABI / opaque-handle stability commitments

## Practical Summary

OpenMeta is no longer blocked by read-path quality for adoption-oriented
transfer work.

The main opportunity now is to make the current bounded transfer core easier
to use and easier to trust across the primary export targets, instead of
continuing to expand read-only surface area first.
