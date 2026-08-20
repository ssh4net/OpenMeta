# Metadata Support Matrix (Draft)

This document summarizes the current metadata read-path coverage in OpenMeta.
OpenMeta does not decode, decompress, demosaic, or render image pixels.

It is meant to answer four basic questions:
- which containers are scanned
- which metadata families are decoded into `MetaStore`
- where display-name mapping exists
- what can be dumped or exported today

## Status Labels

- `Yes`: supported in current code
- `Partial`: supported, but still bounded or best-effort
- `No`: not supported yet

Host integrations can query the same kind of runtime support information with
`openmeta/metadata_capabilities.h`. That API reports read, structured decode,
transfer preparation, target edit, and raw-preservation support by target
format and metadata family.

Some capabilities depend on build-time discovery. Structured XMP requires
Expat, Deflate-compressed metadata requires zlib, JPEG XL `brob` metadata
requires Brotli, and ranked fuzzy search requires explicitly enabling and
finding RapidFuzz C++. The configured build report and runtime capability API
are authoritative for a particular binary.

For the public camera RAW read-depth plan against ExifTool-style coverage, see
[raw_read_parity_plan.md](raw_read_parity_plan.md).

## Coverage Snapshot

Current tracked-gate status:

- EXIF tag-id compare gates are passing on tracked `HEIC/HEIF`, `CR3`, and
  mixed RAW corpora.
- Standalone EXIF/TIFF payload recovery is covered for files with a short
  non-TIFF prefix or a malformed JPEG prefix before the `Exif` preamble.
- EXR header metadata compare is passing for the documented
  name/type/value-class contract.
- Sidecar export paths (`lossless` and `portable`) are covered by baseline and
  smoke tests.
- MakerNote coverage is tracked by baseline gates with broad vendor support;
  unknown vendor tags are preserved as raw metadata for lossless workflows.
  Current public naming includes Fujifilm flash white-balance and native RAF
  firmware fields, plus selected model-specific Sony correction-offset routes.
- Decoded vendor MakerNote sub-IFDs are interpreted/query metadata. Writers do
  not reconstruct MakerNote blobs from those decoded fields. Compatible-file
  `Keep` preserves the original raw payload as unverified opaque bytes; it does
  not relocate nested offsets, repair vendor checksums, or prove semantic
  readability after repacking. Explicit `Rewrite` drops when those operations
  are unavailable. Hosts can inspect this boundary with
  `makernote_transfer_audit_from_store(...)`. The separate
  `makernote_layout_transfer_audit_from_store(...)` recognizes canonical Nikon
  type 1/type 3 offset layouts and validates standard embedded-TIFF structure
  for type 3 without claiming vendor-private offset or checksum validation.
- Metadata-family presence gates for XMP, ICC, IPTC-IIM, Photoshop IRB, and
  JUMBF/C2PA are clean on the tracked still-image corpus. Current read coverage
  includes EXIF/TIFF-carried ICC/IPTC payloads, bare JPEG APP1 XMP packets, and
  XMP packets using alternate `xmpmeta` namespace prefixes.
- BMFF edge-path tests include `iloc` construction-method-2 relation variants
  and safe-skip handling for invalid references.
- Dedicated synthetic read-release gates decode metadata from AVIF, GIF, and
  JXL containers. Separate SR2/SRF gates exercise their shared TIFF metadata
  carrier only; they do not claim native legacy SR2/SRF private-structure
  coverage.

## Container Coverage

| Container / input type | Block discovery | Structured decode in `simple_meta_read(...)` | Notes |
| --- | --- | --- | --- |
| JPEG | Yes | Yes | EXIF, standard and bare APP1 XMP, extended XMP, ICC, MPF, Photoshop IRB, comments, vendor APP blocks, and bounded JUMBF/C2PA; leading-segment scan supports bounded positional callbacks without reading entropy data |
| PNG | Yes | Yes | EXIF, XMP, ICC, structured PNG text, and bounded JUMBF/C2PA; chunk scan supports bounded positional callbacks without reading pixel payloads |
| WebP | Yes | Yes | EXIF, XMP, ICC, and bounded JUMBF/C2PA; chunk scan supports bounded positional callbacks without reading image payloads |
| GIF | Yes | Partial | XMP, ICC, and structured comments; extension scan and payload fetch support bounded positional callbacks without reading raster sub-block payloads |
| TIFF / DNG / TIFF-based RAW | Yes | Yes | EXIF, MakerNote, XMP, IPTC, Photoshop IRB, ICC, GeoTIFF, and bounded JUMBF/C2PA |
| CRW / CIFF | Yes | Partial | Recursive CIFF directories, stable scalar/subtable decode, derived EXIF bridge, and bounded native Canon CIFF naming/projection |
| RAF / X3F | Partial | Partial | RAF includes header-declared preview-JPEG EXIF/XMP discovery, FujiIFD/TIFF follow path, native RAF header/directory geometry tags, RAFData geometry projection, and standalone XMP fallback; X3F includes header fields, known PROP properties, section-directory JPEG metadata follow path, and legacy embedded-EXIF fallback |
| JP2 | Yes | Yes | EXIF, XMP, IPTC, ICC, and GeoTIFF; box scan supports bounded positional callbacks without reading codestream payloads |
| JXL | Yes | Yes | EXIF, XMP, and bounded JUMBF/C2PA; supported `brob` wrapped metadata is decoded and box scan supports bounded positional callbacks |
| HEIF / AVIF / CR3 | Yes | Partial | EXIF, XMP, ICC, CR3 maker blocks, BMFF derived fields including `avif`/`avis`/`avio` AVIF brands, and bounded JUMBF/C2PA; structural/item scan supports bounded positional callbacks without reading `mdat` payloads |
| EXR | n/a via `scan_auto(...)` | Yes | Header attributes only; bounded positional header traversal stops before pixel/chunk data; no pixel decode |

## Camera RAW Support Tiers

Camera RAW extensions are not equivalent to independent complete decoders.
Several families use the shared TIFF/EXIF path, and structured decode depth can
differ from raw block preservation.

| Camera RAW lane | Metadata read | Vendor-private interpretation | Metadata write / transfer |
| --- | --- | --- | --- |
| DNG | Strong TIFF/EXIF/DNG baseline | DNG tags plus available MakerNote interpretation | Bounded TIFF-backed DNG target; not a RAW pixel writer |
| CR2, NEF/NRW, ARW, RW2, ORF, PEF, and other TIFF-based RAW | Yes through the shared TIFF/EXIF carrier | Varies by vendor, model, MakerNote version, and private table | No general native vendor-RAW writer |
| Legacy Sony SR2/SRF | Shared TIFF metadata carrier covered | Older native private structures remain partial | No native writer |
| CR3 | Yes through the dedicated ISO-BMFF lane | Bounded metadata graph, EXIF/XMP/ICC, and Canon maker metadata | Bounded metadata graph edit; not a CR3 image encoder |
| CRW/CIFF | Yes through a dedicated native lane | Partial | No native writer |
| RAF and X3F | Partial dedicated native lanes | Partial | No native writer |

Vendor metadata for Phase One/Leaf, Panasonic, Olympus, Pentax, Kodak, Minolta,
Samsung, Ricoh, Apple, DJI, Google, FLIR, and other families is interpreted
where layouts are stable and testable. This is not a claim that every model or
private table is decoded. For the detailed family gaps, see
[raw_read_parity_plan.md](raw_read_parity_plan.md).

## Metadata Family Coverage

| Metadata family | Decode | Name mapping | Dump / export | Notes |
| --- | --- | --- | --- | --- |
| EXIF (`MetaKeyKind::ExifTag`) | Yes | Yes | Yes | Standard EXIF plus pointer IFDs, including current EXIF 3.1 learning/development/correction/noise tag names and bounded correction/noise value labels |
| MakerNote | Partial / Yes | Partial / Yes | Lossless yes; portable limited | Broad vendor coverage; unknown tags may remain raw; selected version/firmware payloads, live-vendor scalar fields, Fujifilm flash white-balance and native RAF firmware names, selected Sony correction-offset routes, Reconyx scalar/string-coded fields, Microsoft stitch fields, Nintendo category fields, Sanyo public-context scalar fields, current Canon RF/Nikon Z lens labels, and an ambiguous Pentax Sigma/Samsung/Tokina lens-family label have bounded display helpers |
| XMP (`MetaKeyKind::XmpProperty`) | Yes | Native schema/path | Yes | Requires Expat at build time; known nested namespaces use portable prefixes and unknown nested namespaces use a collision-safe `nsu_<namespace-uri-hex>:` segment that preserves full namespace identity |
| ICC (`IccHeaderField`, `IccTag`) | Yes | Yes | Yes | Header fields plus tag table; raw tag payload preserved |
| IPTC-IIM (`IptcDataset`) | Yes | Yes | Yes | Raw dataset bytes preserved |
| Photoshop IRB (`PhotoshopIrb`) | Yes | Partial / Yes | Yes | Raw resources preserved, bounded interpreted subset, fixed-layout, Lightroom workflow, IPTC-NAA byte count, working-path and numbered clipping-path record summaries, descriptor-header, scalar/class/alias/reference, enum, and nested list/object summaries, embedded IPTC/XMP/ICC decode |
| MPF | Yes | Yes | Yes | Basic TIFF-IFD decode |
| GeoTIFF (`GeotiffKey`) | Yes | Yes | Yes | GeoKeyDirectoryTag decode |
| BMFF derived fields (`BmffField`) | Yes | Yes | Yes | `ftyp` brand names including `avif`/`avis`/`avio` AVIF-compatible brands, item-info, `ipco`, `ipma`, `iref`, `grpl`, `iloc`/`idat`, bounded `grid`/`iovl`/`iden` construction semantics, graph summaries with component roles, member item IDs, semantic/relation counts and policy hints, aux semantics, primary item properties, primary metadata-carrier flags, primary sidecar summaries, primary-scene summaries, and bounded primary-linked image roles |
| JUMBF / C2PA (`JumbfField`, `JumbfCborKey`) | Partial | Yes | Yes | Draft structural and semantic layer with box labels; not full conformance |
| EXR attributes (`ExrAttribute`) | Yes | Native names | Yes | Header attributes only |

## Important Bounded Areas

### CRW / CIFF

OpenMeta now does more than a pure derived-EXIF bridge:
- common native CIFF tags are named
- a bounded set of native subtables is projected
- stable scalar native CIFF fields are decoded where the layout is clear

It is still a partial lane compared to the deepest legacy Canon tooling.

### RAF

OpenMeta now follows both RAF metadata carriers that matter for common camera
files:
- the header-declared preview JPEG is scanned for standard EXIF/XMP-style
  metadata
- the header-declared FujiIFD/TIFF area is scanned for native RAF/raw fields
- native RAF header, directory, and RAFData-derived fields are classified as
  source-specific metadata for rendered-transfer safety

Deeper model-specific RAF sections remain a partial lane.

### X3F

OpenMeta now has a bounded native Sigma X3F lane:
- common X3F header fields are decoded as `x3f_header`
- stable header-extension adjustment fields are decoded as `x3f_header_ext`
- known `PROP` properties are decoded as `x3f_prop`
- section-directory JPEG metadata is followed for embedded EXIF/XMP-style
  metadata blocks, with the older embedded-EXIF scan kept as fallback

Deeper image-processing/compression sections remain partial and should only be
promoted when fields can be typed, named, and safety-classified.

### Photoshop IRB

OpenMeta preserves raw IRB resources and also decodes a bounded interpreted
subset. That subset includes common fixed-layout resources and descriptor-header
summaries such as:
- `Photoshop2Info`
- `Photoshop2ColorTable`
- `ResolutionInfo`
- `AlphaChannelsNames`
- `DisplayInfo`
- `PStringCaption`
- `BorderInformation`
- `BackgroundColor`
- `VersionInfo`
- `PrintFlags`
- print-flag byte fields
- `EffectiveBW`
- `QuickMaskInfo`
- `RawImageMode`
- `JPEG_Quality`
- `GridGuidesInfo`
- legacy halftone, transfer-function, duotone-image, and EPS byte summaries
- `PhotoshopBGRThumbnail` / `PhotoshopThumbnail` headers
- `SpotHalftone`
- `UnicodeAlphaNames`
- `AlphaIdentifiers`
- `JumpToXPEP`
- `PrintScaleInfo`
- `PixelInfo`
- `AutoSaveFilePath`
- `AutoSaveFormat`
- `XMLData`
- `ImageReadyVariables`
- `ImageReadyDataSets`
- `LightroomWorkflow`
- `IPTCDataBytes`
- `ColorSamplersResource` / `ColorSamplersResource2` headers and records
- `WorkingPath` and numbered path resources as byte counts plus record
  counts/selectors
- descriptor-header summaries, bounded scalar, class, alias, enum, and `obj `
  reference item bodies, raw-data descriptor byte counts, and bounded nested
  list/object traversal with item paths, depths, list indices, and
  parsed-value counts for resources such as
  `LayerComps`,
  `MeasurementScale`, `HDRToningInfo`, `PrintInfo`, `TimelineInfo`,
  `SheetDisclosure`, `OnionSkins`, `CountInfo`, `PrintInfo2`, `PrintStyle`,
  `PathSelectionState`, and `OriginPathInfo`
- `ChannelOptions`
- `PrintFlagsInfo`
- `ClippingPathName`
- `MacintoshPrintInfo`, `MacintoshNSPrintInfo`, `WindowsDEVMODE`,
  `AlternateDuotoneColors`, `AlternateSpotColors`, ObsoletePhotoshopTag1,
  ObsoletePhotoshopTag2, and ObsoletePhotoshopTag3 byte counts
- embedded IPTC-NAA, ICC, EXIF, EXIF2, and XMP resource byte counts

When enabled, embedded IPTC-IIM, XMP, and ICC payloads in IRB resources are
also decoded into their regular OpenMeta entry families.

Current Photoshop IRB interpretation status:

| Resource area | Status | Notes |
| --- | --- | --- |
| Raw IRB resources | Preserved | Every accepted resource keeps a lossless `PhotoshopIrb` raw entry. |
| Embedded IPTC/XMP/ICC/EXIF carriers | Interpreted where enabled | Payload bytes remain preserved; enabled decoders also emit regular family entries. |
| Fixed-layout scalar/string resources | Interpreted | Resolution, alpha/caption strings, version, print flags, quick mask, JPEG quality, URL/list, pixel info, autosave strings, `XMLData`, ImageReady text, Lightroom workflow text, and similar bounded fields. |
| Geometry/display/color-sampler/path headers | Interpreted / record-summary | Display/grid/thumbnail/color-sampler headers are decoded; path resources expose byte counts, record counts, and selectors without interpreting Bezier payloads. |
| Descriptor-backed Photoshop resources | Descriptor header plus bounded item summaries | Descriptor version, remaining byte count, class ID/name, item count, complete value bodies for `bool`, `long`, `comp`, `doub`, `UntF`, `TEXT`, `enum`, `type`, and `GlbC`, opaque byte counts for `alis` and `tdta`, nested object/list paths, depths, list indices, list/object counts, and parsed-value count fields are emitted. Bounded `obj ` streams expose ordered property, class, enumerated, offset, identifier, index, and name references with paths, depths, indices, class IDs/names, subtype values, and parsed counters. Alias/raw payload contents and full Photoshop action execution semantics remain out of scope for this subset. |
| Legacy halftone/transfer/duotone/EPS resources | Header / byte-count only | OpenMeta emits byte counts and first header words where safe, without claiming full Photoshop semantics. |
| Proprietary, obsolete, or OS-specific resources | Raw-preserved, selected byte counts | Kept losslessly; selected stable resource IDs expose byte counts without claiming payload semantics. |

This is useful, but it is still not full Photoshop-resource parity.

Descriptor reference traversal accepts at most 64 items in one `obj ` value
and 128 reference items across one descriptor. Unknown, incomplete, or excess
reference data stops traversal and emits `DescriptorItemParseTruncated`; a
partially decoded reference element is never emitted.

### BMFF (`HEIF` / `AVIF` / `CR3`)

OpenMeta now has a bounded semantic model on top of raw item discovery:
- `ftyp.*`, including brand names and compatible-brand counts
- primary item properties, primary metadata-carrier flags, primary sidecar
  counts/flags for linked metadata and image sidecars, content-bound
  C2PA/JUMBF sidecar policy hints, and compact primary-scene node/edge
  summaries
- `ipco` property-container summaries with total, known, unknown, and
  per-known-type property counts
- `ipma` item-property association rows with item ids, property indices,
  essential flags, and known property type names
- per-known-property `ipma.<type>` association, primary-association, and
  essential-count rollups for common primary-item properties
- `iinf/infe` item-info rows
- item type-name and semantic labels for EXIF, XMP, JUMBF, C2PA, ICC profile,
  image, URI, auxiliary, thumbnail, derived-image, and content-description items
- aggregate item semantic counters for known, metadata, image, EXIF, XMP,
  JUMBF, C2PA, ICC profile, auxiliary, derived, thumbnail,
  content-description, URI, and JSON roles
- whole-scene item graph counters for known items, image/metadata/content-bound
  metadata nodes, selected image-role nodes, relation edges, item groups, and
  conservative content-bound metadata / multi-image policy hints including
  explicit multi-image policy text
- bounded scene graph component counters for observed relation graph nodes,
  connected components, image and multi-image components, isolated image
  nodes, components with content-bound metadata, per-component role text,
  ordered member item IDs, known/unknown and semantic node counts, isolated
  state, typed auxiliary/derived/thumbnail/content-description edge counts,
  alpha/depth/disparity/matte auxiliary edge counts, primary graph-component
  nodes/edges, primary component content-bound flags, primary component
  multi-image candidates and policy text, and primary component content-bound
  metadata policy
- typed `iref.<type>.*` rows, including direction-correct endpoint roles and
  named item-id aliases for `auxl`, `dimg`, `thmb`, and `cdsc`; literal
  `from_item_id` and `to_item_id` rows remain unchanged
- bounded `grpl` item-group rows and per-group-type summaries, including group
  semantics and ordered entity roles for `altr`, `ster`, and `pymd`
- bounded `iloc`/`idat` item-data layout summaries, including construction
  methods, extent counts, total extent byte counts, idat byte counts, and
  primary-item location aliases
- bounded `grid`, `iovl`, and `iden` derived-image construction rows with
  ordered `dimg` source IDs, descriptor/source/construction validity, grid
  rows, columns, tile coordinates and output size, overlay canvas/background
  and signed source offsets, identity sources, primary aliases, bounded
  method-2 item-offset descriptor chains, descriptor reference depth,
  derived-graph cycle/missing-source/truncated-reference validation, and a
  source-bound `container_graph` concept for query/transfer diagnostics
- `tili` tiled-image items and bounded `tilC` version-0 configuration fields,
  including tile width/height, up to eight extra dimensions, required
  single-`ispe`/single-`tilC` association checks, ceil-divided tile
  rows/columns, overflow-checked expected tile counts, `dinf`/`dref`/`deti`
  mapping, internal `tile_item_type` and nested `tipa` associations, bounded
  external URL components, logical-`iloc` offset tables, explicit or
  sequentially inferred tile sizes, empty-tile state, complete-configuration
  validity, and source-bound query/transfer diagnostics
- graph summaries
- `auxC`-typed auxiliary semantics
- bounded primary-linked image-role fields, separate primary inbound
  derived-image and outbound derived-source summaries, compact primary sidecar
  aggregate fields, and compact primary-scene aggregate fields
- primary `colr` summaries for `nclx`/`nclc` color fields and ICC profile-size
  carriers
- primary `pasp`, `pixi`, and `clap` item-property summaries for pixel aspect
  ratio, pixel component bit depth, and clean aperture

This is intentionally smaller than a full QuickTime/BMFF semantic model.
Derived descriptors are interpreted from complete method-0 file extents,
method-1 `idat` extents, and bounded method-2 item-offset chains. Method-2
resolution follows ordered `iref` `iloc` references, rejects reserved or
out-of-range indexes, detects recursion cycles, validates every source range,
and never materializes a complete logical item. External data references other
than bounded tiled-image `deti` URL state and incomplete extent inventories
remain structural-only. Tiled-image offset-table validation scans at most
262144 entries and emits at most 64 rows; URL components retain at most 512
bytes for field output. Larger valid structures remain visible through counts
and truncation fields but do not receive complete-configuration validity.

### JXL

OpenMeta decodes:
- direct `Exif`
- direct `xml `
- direct `jumb`
- direct `c2pa`
- wrapped `brob` forms for those same realtypes

Other `brob` realtypes are still out of scope.

### JUMBF / C2PA

Current support is intentionally draft:
- structural BMFF box decode
- JUMBF box labels from parsed `jumd` boxes
- bounded CBOR traversal
- draft `c2pa.semantic.*` projection
- optional signature and certificate-chain diagnostic scaffolding

What this means in practice:
- OpenMeta can expose useful manifest / claim / signature / ingredient shape
  information
- the current verification result must not be used as an asset-authenticity or
  trust gate: it does not establish the manifest's hard binding to the complete
  asset; cryptographic signature success is reported as
  `signature_verified_only`
- `verify_require_trusted_chain` and
  `--c2pa-verify-require-trusted-chain` additionally require a trusted chain
  bound to the signature key, but do not establish asset binding
- OpenMeta does not claim full C2PA manifest semantics, asset binding, or policy
  validation

Structural JUMBF and C2PA metadata decode remains available independently of
the optional verification scaffold.

## Tool-Level Behavior

| Tool | Purpose | Current state |
| --- | --- | --- |
| `metaread` | Human-readable metadata listing | Shows decoded entries with mapped names where available |
| `metavalidate` | Metadata validation | Reports decode-status and validation issues with machine-readable issue codes |
| `metadump` | Sidecar and preview dump tool | Supports `lossless` and `portable` sidecar output plus preview extraction |
| `thumdump` | Preview extractor | Extracts embedded preview candidates |
| `metatransfer` | Transfer/edit smoke tool | Exercises the transfer core for supported target families |

## Main Current Gaps

- independently authored conformance-file validation for the recently
  standardized BMFF tiled-image layout
- additional `JXL brob` realtypes beyond `Exif`, `xml `, `jumb`, and `c2pa`
- full `JUMBF/C2PA` semantics and policy validation
- deeper RAF model-specific native tables and X3F image-processing sections
  beyond the current bounded carrier/header/property lanes
- full Photoshop action execution semantics, opaque alias/raw descriptor
  payload interpretation, and broader IRB interpretation beyond the current
  subset

## Related Docs

- [metadata_transfer_plan.md](metadata_transfer_plan.md)
- [development.md](development.md)
