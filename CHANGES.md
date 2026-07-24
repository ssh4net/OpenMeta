# OpenMeta Changes

## 0.4.83 - 2026-07-24

Changes compared with `0.4.82`.

### Added

- Added normalized IPTC-IIM roles for non-equivalent object-type,
  object-attribute, subject, editorial-state, fixture, action, cycle, language,
  reference, and contact fields without forcing lossy XMP aliases.
- Added scoped content-location, prior-envelope-reference, originating-software,
  editorial-workflow, and editorial-contact records.
- Added typed editorial release and expiration date-time candidates with full
  date/time source-entry provenance.
- Added matching append-only C++ and thin Python roles and record kinds.

### Changed

- Prior-envelope references and originating-software records are source-bound
  for rendered transfer, while editorial contacts carry independent
  personal-contact sensitivity.
- Descriptive queries now classify the legacy IPTC workflow tail as taxonomy,
  editorial, document-lineage, source, or contact semantics.

## 0.4.82 - 2026-07-21

Changes compared with `0.4.81`.

### Added

- Added scoped IPTC Extension image-region boundary interpretation for
  rectangle, circle, and polygon shapes with explicit pixel or relative
  coordinate units, numeric field candidates, normalized rectangle/circle
  values, polygon vertex records, and complete source-entry provenance.
- Added XMP Media Management manifest-item and version records for manifest
  link/resolution fields and version comments, modifier, modification date,
  and identifier wrappers.
- Added append-only C++ and thin Python enums for image-region shapes,
  coordinate units, boundary roles, and manifest/version record kinds.

### Changed

- XMP Media Management version wrapper fields now query as document history;
  image-region boundaries require a target image specification, while
  manifest/version records remain source-bound for rendered-image transfer.

## 0.4.81 - 2026-07-20

Changes compared with `0.4.80`.

### Added

- Added scoped IPTC Extension records for controlled-vocabulary terms,
  registry entries, and image-region names, identifiers, content types, and
  roles.
- Added structured XMP Media Management interpretation for resource
  references, ingredients, history and version events, manifest references,
  and bounded pantry identity/format fields.
- Added exact `registry`, `image_region`, `document_lineage`, and
  `document_history` query semantics with matching append-only C++ and thin
  Python enums.

### Changed

- Document, registry, resource-reference, event-history, and pantry identity
  records are now source-bound for rendered transfer instead of being treated
  as generally safe descriptive metadata.
- Image-region records now require target image specifications. Boundary
  coordinates and arbitrary pantry payloads remain lossless raw metadata until
  their context can be normalized without guessing.

## 0.4.80 - 2026-07-17

Changes compared with `0.4.79`.

### Added

- Added normalized descriptive roles for urgency, category, supplemental
  category, instructions, creator title, transmission reference, and caption
  writer across their equivalent IPTC-IIM and Photoshop XMP fields.
- Added exact accessibility, controlled-taxonomy, and document-identity query
  semantics for IPTC Core accessibility text, genre/scene/subject codes,
  Dublin Core identifiers, XMP resource identifiers, and XMP Media Management
  document, instance, original-document, and rendition identifiers.
- Added structured PLUS interpretation for end users, image creators, image
  suppliers, delivered-image identity, residual license policy, image IDs,
  copyright registration/publication fields, and model-age disclosure.

### Changed

- Equivalent legacy editorial values now use cross-family preference and
  conflict handling, while supplemental categories, scene codes, subject
  codes, resource identifiers, and license-document references remain
  additive collections.
- Deprecated flat IPTC Core creator-contact properties now resolve into the
  same scoped contact record as the structured form.
- C++ and thin Python enums expose the same new roles, record kinds, and
  `editorial`, `accessibility`, `taxonomy`, and `document_identity` semantics.

## 0.4.79 - 2026-07-17

Changes compared with `0.4.78`.

### Added

- Added structured descriptive record kinds and normalized roles for creator
  contacts, events, people, organizations, products, artwork/objects, encoded
  rights expressions, rights holders, licensors, licensees, licenses, and
  model/property releases.
- Added exact query semantics for contact, event, person, organization,
  product, artwork, encoded-rights-expression, and release fields.
- Added an independent sensitivity classification for personal contact,
  person identity, location, and legal-rights metadata. C++ candidates,
  transfer diagnostics, and thin Python dictionaries expose the same values.
- Added interpretation for PLUS license dates, constraints, transaction and
  project identifiers, terms, conditions, and release status/identifiers.

### Changed

- Structured descriptive conflicts and preference are isolated by record kind,
  record scope, and language where applicable, so equal roles in different
  people, products, artwork, license, or release records remain independent.
- Technical transfer safety and policy sensitivity are reported separately;
  privacy-sensitive metadata can remain technically portable without being
  treated as automatically appropriate for publication.
- Creator-contact, structured editorial, rights-expression, and PLUS fields
  now participate in exact descriptive query results without consuming new
  bits from the full legacy `MetadataQueryMatchTerm` mask.

## 0.4.78 - 2026-07-15

Changes compared with `0.4.77`.

### Added

- Added descriptive concept roles and exact query semantics for copyright
  notices/status, rights owners and statements, usage terms, license and
  licensor identifiers, credit lines, source, and digital-source type across
  bounded EXIF, IPTC IIM, Dublin Core, XMP Rights, Photoshop, IPTC Extension,
  and PLUS fields.
- Added generic structured-record scope provenance to C++ candidates, transfer
  diagnostics, and thin Python dictionaries so PLUS owner/licensor names and
  identifiers retain their record association.

### Changed

- Rights-holder and licensor values use additive collection semantics, while
  localized copyright notices and usage terms compare only within the same
  normalized language.
- Descriptive exact semantic matches can report rights, license, credit, or
  source with zero legacy match-term bits, preserving the existing 32-bit
  `MetadataQueryMatchTerm` ABI.
- XMP query leaf extraction now stops before qualifiers, allowing localized
  properties such as `UsageTerms[@xml:lang=...]` to classify correctly.

## 0.4.77 - 2026-07-15

Changes compared with `0.4.76`.

### Added

- Added the experimental `Descriptive` concept kind with roles for title,
  headline, description, creator, keywords, and common created/shown location
  fields across standard EXIF, IPTC IIM, and XMP schemas.
- Added normalized language provenance to concept candidates, transfer
  diagnostics, and thin Python dictionaries.

### Changed

- Localized scalar conflicts and preference are evaluated per language and,
  for structured locations, per `LocationCreated` or `LocationShown[n]` scope.
- Creator, keyword/subject, and location-identifier values now use additive
  collection semantics: distinct values remain preferred while duplicate
  normalized values select the highest-priority source without false
  conflicts.

## 0.4.76 - 2026-07-14

Changes compared with `0.4.75`.

### Added

- Added distinct GPS concept roles for EXIF/XMP destination coordinates and
  IPTC Extension `LocationShown` / `LocationCreated` coordinates.
- Added structured-location scope provenance to C++ candidates, transfer
  diagnostics, and thin Python dictionaries.

### Changed

- Structured-location conflict and preference handling now compares duplicate
  values only within the same `LocationShown[n]` or `LocationCreated` scope,
  without conflating these coordinates with camera position.
- EXIF destination latitude/longitude candidates now retain their matching
  reference tags as source-entry provenance.

## 0.4.75 - 2026-07-14

Changes compared with `0.4.74`.

### Added

- Added bounded EXIF `DateTime*` composition with matching `OffsetTime*` and
  `SubSecTime*` entries, including source-entry provenance and normalized
  subsecond precision through the C++ and thin Python concept APIs.

### Changed

- Offset-aware date/time conflicts now compare normalized UTC instants, while
  missing timezone or subsecond fields remain lower-precision evidence rather
  than automatic conflicts.
- Camera-GPS concept resolution now accepts XMP GPS properties only from the
  EXIF XMP schema and pairs split fields within the same property scope, so
  IPTC Extension `LocationShown` and `LocationCreated` coordinates are not
  conflated with camera position.

## 0.4.74 - 2026-07-14

Changes compared with `0.4.73`.

### Added

- Added bounded BMFF `dinf`/`dref`/`deti` decoding for tiled-image data
  references, including internal offset-table state and bounded external URL
  components.
- Added conditional internal `tile_item_type` and nested `tipa` decoding with
  8-bit/16-bit association records, `ipco` index validation, and property-type
  projection.
- Added logical-`iloc` offset-table validation, explicit and sequentially
  inferred tile sizes, empty-tile state, bounded row output, and complete
  tiled-image configuration validity.

### Changed

- Internal tiled-image data reads now permit only the selected root `deti`
  reference; generic item reads continue to reject non-local data references.
- Updated public BMFF support, interpretation, API stability, RAW parity, and
  development documentation for the complete bounded tiled-image field
  contract.

## 0.4.73 - 2026-07-13

Changes compared with `0.4.72`.

### Added

- Added bounded BMFF `tilC` version-0 configuration decoding for tile width,
  tile height, up to eight extra dimensions, and conditional payload byte
  counts without copying conditional data.
- Added `tili` property-relationship, tile-grid, expected-tile-count, overflow,
  and aggregate validity fields with fail-closed malformed-input handling.
- Added the source-bound `TiledImageConfiguration` container-graph concept,
  rendered-transfer diagnostic text/token, thin Python enum exposure, and
  focused decoder/concept/transfer regressions.

### Changed

- `tilC` is now a known BMFF item property in `ipco`/`ipma` summaries.
- Updated public BMFF support, interpretation, API stability, RAW parity,
  transfer-policy, and development documentation for the bounded tiled-image
  contract.

## 0.4.72 - 2026-07-13

Changes compared with `0.4.71`.

### Added

- Added bounded Photoshop action-descriptor `obj ` reference traversal for
  property, class, enumerated, offset, identifier, index, and name forms.
- Added ordered reference path, depth, index, type, class, subtype-value, and
  aggregate counter fields for host inspection.
- Added fail-closed malformed, unknown-type, per-reference limit, aggregate
  limit, and nested-list reference regressions.

### Changed

- Descriptor references are limited to 64 items per `obj ` value and 128
  items per descriptor; excess or incomplete data sets the existing
  `DescriptorItemParseTruncated` field.
- Updated public Photoshop IRB support, interpretation, API stability, and
  development documentation for the expanded descriptor grammar.

## 0.4.71 - 2026-07-13

Changes compared with `0.4.70`.

### Added

- Added bounded Photoshop action-descriptor value decoding for signed 64-bit
  `comp` integers, `type` and `GlbC` class values, and opaque `alis` byte
  counts.
- Added descriptor type names/codes and parsed per-type counters for large
  integers, local/global classes, and aliases.
- Added focused valid and truncated-payload regressions for the expanded
  descriptor grammar.

### Changed

- Updated public interpretation, support, API stability, and development
  documentation for the expanded Photoshop IRB descriptor subset.
- Recorded experimental tiled-image configuration as the remaining BMFF
  derived-image tail instead of claiming a stable field contract.

## 0.4.70 - 2026-07-13

Changes compared with `0.4.69`.

### Added

- Added bounded zero-copy BMFF construction-method-2 descriptor reads through
  ordered `iref` `iloc` item references, including recursive logical-item
  offsets, implicit single-source indexes, and compatibility mapping for
  multi-extent no-index layouts.
- Added fail-closed item-reference depth, cycle, overflow, source-range, and
  extent-index validation, plus descriptor reference-depth fields.
- Added derived-image graph cycle, self-reference, missing-source, depth,
  truncated-reference, and aggregate validity fields with primary-item
  aliases.
- Added `tili` tiled-image item classification while keeping experimental
  `tilC` configuration structural-only.

### Changed

- Derived construction validity now also requires a valid, declared source
  graph instead of accepting missing or cyclic `dimg` source items.
- Updated public BMFF support, interpretation, RAW parity, development, and
  compatibility-dump documentation for item-offset and graph validation.

## 0.4.69 - 2026-07-10

Changes compared with `0.4.68`.

### Added

- Added bounded BMFF `grid`, `iovl`, and `iden` construction interpretation,
  including ordered source items, grid tile coordinates, output dimensions,
  overlay background color and signed source offsets, and identity sources.
- Added construction validity fields for descriptor availability, descriptor
  parsing, source-count constraints, and complete derived-image usability.
- Added direct bounded descriptor reads across local-file and `idat` `iloc`
  extents without copying complete item payloads.
- Added a source-bound `DerivedImageConstruction` container-graph concept role,
  rendered-transfer diagnostic text/token, and thin Python enum exposure.

### Changed

- Classified `grid`, `iovl`, and `iden` item types as derived images in BMFF
  semantic summaries.
- Updated public BMFF support, interpretation, compatibility, API stability,
  RAW parity, and development documentation for derived-image constructions.

## 0.4.68 - 2026-07-10

Changes compared with `0.4.67`.

### Added

- Added direction-correct semantic endpoint roles and named item-id aliases
  for BMFF `auxl`, `dimg`, `thmb`, and `cdsc` item references while preserving
  the literal source/target fields.
- Added separate primary derived-image and derived-source summaries so inbound
  derived items are not confused with source images used by a derived primary.
- Added semantic and per-entity role fields for bounded BMFF `altr`, `ster`,
  and `pymd` item groups.

### Changed

- Corrected primary sidecar, auxiliary subtype, and scene-component
  interpretation to follow the encoded direction of known BMFF item-reference
  types.
- Updated public BMFF support, interpretation, RAW parity, and development
  documentation for direction-aware relations and semantic item groups.

## 0.4.67 - 2026-07-10

Changes compared with `0.4.66`.

### Added

- Added bounded BMFF component role and membership rows, including ordered
  member item IDs, known/unknown and semantic node counts, isolated-state,
  and typed auxiliary, derived-image, thumbnail, content-description, alpha,
  depth, disparity, matte, and other relation-edge counts.
- Added independent-image component coverage and direct `container_graph`
  concept/transfer-diagnostic routing for per-component metadata and
  multi-image policy fields.
- Added compatibility-dump coverage for BMFF component role and membership
  fields.

### Changed

- Updated public BMFF interpretation, support, compatibility, API stability,
  and development documentation for component membership and typed relation
  summaries.

## 0.4.66 - 2026-07-09

Changes compared with `0.4.65`.

### Fixed

- Forwarded an explicit `tsl-robin-map_DIR` package hint into the nested
  scikit-build configure used by CMake wheel targets and install-time wheel
  builds. This supports nanobind packages that use an external
  `tsl-robin-map` installation when prefix-only lookup is insufficient.

## 0.4.65 - 2026-06-22

Changes compared with `0.4.64`.

### Added

- Added bounded BMFF per-component scene graph rows for component index,
  node counts, image/metadata roles, primary-component membership, and
  conservative content-bound metadata / multi-image policy text.
- Added compatibility dump coverage for BMFF container-graph policy fields.
- Added decoded-BMFF transfer diagnostic coverage so rendered-image drops for
  content-bound metadata and multi-image scene policy are verified from parsed
  fixture data.
- Added Photoshop IRB coverage for numbered clipping-path resources in the
  `0x07D0` range, including path byte counts, record counts, and selector
  rows.

### Changed

- Updated public interpretation, support, API stability, compatibility-dump,
  and development docs for BMFF per-component policy rows and numbered
  Photoshop IRB path resources.

## 0.4.64 - 2026-06-22

Changes compared with `0.4.63`.

### Added

- Added BMFF multi-image policy text fields for whole-scene and primary
  graph-component multi-image candidates.
- Added `ContainerGraph` concept resolution and transfer diagnostic coverage
  for BMFF multi-image policy text fields.
- Added Photoshop IRB `PathDataBytes` interpretation for bounded working-path
  and clipping-path resources while continuing to preserve raw path payloads.

### Changed

- Updated public interpretation, support, API stability, and development docs
  for BMFF multi-image policy text and Photoshop IRB path byte-count surfaces.

## 0.4.63 - 2026-06-22

Changes compared with `0.4.62`.

### Added

- Added BMFF scene graph component policy fields for content-bound component
  counts, primary component content-bound flags, and primary component
  multi-image candidates.
- Added `ContainerGraph` concept resolution and transfer diagnostic source
  coverage for BMFF primary graph-component multi-image candidates.
- Added Photoshop IRB byte-count interpretation for
  ObsoletePhotoshopTag1, ObsoletePhotoshopTag2, and ObsoletePhotoshopTag3
  resources while preserving raw resource payloads.

### Changed

- Updated public interpretation, support, API stability, and development docs
  for the expanded BMFF graph-component policy and Photoshop IRB byte-count
  surfaces.

## 0.4.62 - 2026-06-21

Changes compared with `0.4.61`.

### Added

- Added bounded BMFF scene graph component summaries, including graph node
  count, component count, image/multi-image component counts, observed relation
  edge count, primary graph-component node/edge counts, and primary component
  content-bound metadata policy hints.
- Added `ContainerGraph` concept resolution for BMFF content-bound metadata and
  multi-image scene policy fields, plus rendered-transfer diagnostics and
  message tokens for dropping those source-bound fields.
- Added Photoshop IRB byte-count interpretation for MacintoshPrintInfo,
  AlternateDuotoneColors, and AlternateSpotColors resources while preserving
  raw resource payloads.

### Changed

- Updated public interpretation, support, API stability, and development docs
  for the new BMFF scene graph, container-graph diagnostics, and Photoshop IRB
  byte-count surfaces.

## 0.4.61 - 2026-06-20

Changes compared with `0.4.60`.

### Added

- Added BMFF whole-scene item graph summary fields for item counts, known item
  counts, image/metadata/content-bound metadata node counts, selected role node
  counts, relation edge count, item-group count, and conservative
  content-bound metadata / multi-image policy hints.
- Added Photoshop IRB byte-count interpretation for Macintosh NSPrintInfo and
  Windows DEVMODE resources while preserving raw resource payloads.

### Changed

- Updated public interpretation, support, API stability, and development docs
  for the new BMFF scene-policy and Photoshop IRB byte-count surfaces.

## 0.4.60 - 2026-06-18

Changes compared with `0.4.59`.

### Added

- Added BMFF primary-scene per-role edge counters so hosts can distinguish
  linked-item edges from unique linked scene nodes.
- Added Photoshop IRB descriptor parsed-type summary counters and parsed
  maximum depth fields for bounded action-descriptor inspection.
- Added rendered-transfer diagnostic coverage for computational, thermal, and
  stitch source-processing message tokens.

### Changed

- BMFF primary sidecar, scene-node, and linked-item semantic counters now count
  unique linked scene nodes; `primary.linked_item_role_count` and
  `primary.scene_edge_count` remain edge counts.
- Updated public interpretation and API stability docs for the expanded BMFF
  scene and Photoshop IRB descriptor summaries.

## 0.4.59 - 2026-06-18

Changes compared with `0.4.58`.

### Added

- Added BMFF primary-scene node bucket fields for linked auxiliary, alpha,
  depth, derived-image, thumbnail, content-description, image, metadata, and
  content-bound metadata nodes.
- Added Photoshop IRB descriptor item type-code fields alongside descriptor
  item type names and existing bounded descriptor summaries.
- Added transfer diagnostic localization helpers:
  `transfer_concept_diagnostic_message_token(...)` and
  `transfer_concept_diagnostic_message_arguments(...)`, plus Python
  `message_token` and `message_arguments` fields.
- Added IPTC digital-creation date/time composite interpretation for the
  cross-family `Digitized` concept role.
- Added bounded Motorola MakerNote `CustomRendered` value labels.

### Changed

- Updated public API, interpretation, quick-start, and host-integration docs
  for the expanded scene, IRB, date/time, diagnostic, and MakerNote
  interpretation surfaces.

## 0.4.58 - 2026-06-17

Changes compared with `0.4.57`.

### Added

- Added BMFF primary-linked content-bound metadata sidecar flags and counts for
  linked C2PA/JUMBF sidecars, including a conservative
  `requires_target_rewrite` policy token.
- Added bounded Photoshop IRB descriptor `tdta` raw-data byte-count summaries
  without exposing or interpreting the raw descriptor payload.
- Added `transfer_concept_diagnostic_token(...)` and Python diagnostic `token`
  fields for stable host-facing transfer preflight summaries.
- Added explicit XMP `GPSDateTime` / `GPSDateTimeStamp` GPS timestamp
  interpretation and reconciliation with split XMP GPS date/time fields.
- Added regression coverage for stable Apple MakerNote image-capture labels
  while keeping ambiguous Apple flag values unlabeled.

### Changed

- Updated public support, interpretation, API stability, and host-integration
  docs for the expanded BMFF, Photoshop IRB, GPS timestamp, and transfer
  diagnostic surfaces.

## 0.4.57 - 2026-06-17

Changes compared with `0.4.56`.

### Added

- Added bounded Canon RF lens-type display labels for current public RF tail
  IDs.
- Added bounded Nikon Z `LensData0800` `LensID` display labels for current Z
  lens IDs.
- Added a conservative Pentax lens-family display label for an ambiguous
  Sigma/Samsung/Tokina lens value instead of claiming one exact lens.
- Added XMP namespace-rebinding regression coverage to keep decode and portable
  dump behavior URI-based and isolated between packets.

### Changed

- Updated public interpretation, support, API stability, and development docs
  for the expanded bounded lens-label coverage.

## 0.4.56 - 2026-06-17

Changes compared with `0.4.55`.

### Added

- Added standard EXIF 3.1 tag names for learning-intent, development-type,
  lens-correction, shading-correction, and noise-reduction fields.
- Added EXIF 3.1 light-source values, VC-5 compression labeling, and bounded
  correction/noise-reduction display labels.
- Added Fujifilm `WB_GRGBLevelsFlash` MakerNote naming and corrected the native
  RAF header version field name to `FirmwareVersion`.
- Added Sony ILCE-7RM6 routing for decoded `Tag9050d`, `Tag9400c`, and
  model-specific `Tag9416` correction offsets.
- Added `avio` AVIF brand detection for BMFF scanning and field decode paths.

### Changed

- Kept generic MIAF brand handling conservative instead of treating `miaf`
  alone as AVIF without an AV1-specific brand.
- Updated public interpretation, API stability, and development docs for the
  refreshed upstream-delta coverage.

## 0.4.55 - 2026-06-16

Changes compared with `0.4.54`.

### Added

- Added bounded Nintendo CameraInfo category display labels.
- Added bounded Sanyo public-context display labels for main MakerNote quality,
  macro, sequential-shot, on/off state, shutter-release, scene, interval, flash,
  and MOV white-balance scalar values.

### Changed

- Kept Sanyo measurement fields, MP4 numeric fields, text payloads, and
  unsupported values unformatted instead of guessing labels.
- Updated public interpretation, API stability, and development docs for the
  expanded Nintendo/Sanyo display-label coverage.

## 0.4.54 - 2026-06-16

Changes compared with `0.4.53`.

### Added

- Added bounded Microsoft stitch MakerNote display labels for camera-motion and
  map-type scalar values.
- Added bounded Reconyx MakerNote display labels for selected moon phase,
  weekday, flash/illumination, battery-type, and single-character trigger-mode
  values across supported Reconyx subdirectories.

### Changed

- Kept measurement fields, text/count payloads, unsupported private fields, and
  device-specific values unformatted unless the mapping is stable.
- Updated public interpretation, API stability, and development docs for the
  expanded long-tail display-label coverage.

## 0.4.53 - 2026-06-15

Changes compared with `0.4.52`.

### Added

- Added bounded Apple MakerNote scalar labels for AE/AF stability,
  HDR image type, image-capture type, and camera type.
- Added bounded FLIR GPS-valid, JVC quality, and General Imaging macro-mode
  MakerNote labels.

### Changed

- Kept ambiguous Apple fields, FLIR text-valued image-type fields, and
  unsupported private values empty instead of guessing labels.
- Updated public interpretation, API stability, and development docs for the
  expanded live-vendor and long-tail scalar coverage.

## 0.4.52 - 2026-06-15

Changes compared with `0.4.51`.

### Added

- Added bounded EXIF/MakerNote version-value formatter helpers:
  `exif_tag_numeric_value_format(...)` and
  `exif_tag_byte_value_format(...)`.
- Added formatter coverage for selected standard EXIF byte-version payloads,
  Nikon version-like contexts, and Olympus packed firmware values.
- Added thin Python wrappers for the new version-value formatter helpers.

### Changed

- Kept formatted version/firmware values separate from enum-label lookup so
  ambiguous values remain lossless instead of being guessed.
- Updated public interpretation, support, API stability, and development docs
  for the new formatter path and remaining per-model MakerNote blockers.

## 0.4.51 - 2026-06-15

Changes compared with `0.4.50`.

### Added

- Added bounded Photoshop IRB action-descriptor traversal for non-empty nested
  object descriptors and simple list values, including item depth, path, list
  index, and parsed-value count fields.
- Added compact BMFF primary-scene summary fields for primary item count,
  unique linked item count, node count, and edge count around the current
  primary-linked sidecar model.

### Changed

- Updated public interpretation, support, API stability, and development docs
  for nested descriptor traversal and BMFF primary-scene summaries.

## 0.4.50 - 2026-06-12

Changes compared with `0.4.49`.

### Added

- Added bounded Photoshop IRB action-descriptor item summaries for enum values
  plus empty list/object headers. Non-empty nested list/object bodies still
  report truncation instead of guessing recursive semantics.
- Added BMFF primary sidecar summary fields for linked primary items, including
  total sidecar count, metadata/image sidecar flags, and per-role sidecar
  counters for common metadata and image-sidecar semantics.
- Added stable On/Off value labels for additional NikonSettings MakerNote
  fields where the value shape is unambiguous.

### Changed

- Updated public interpretation, support, API stability, and development docs
  for the expanded BMFF, Photoshop descriptor, and NikonSettings coverage.

## 0.4.49 - 2026-06-10

Changes compared with `0.4.48`.

### Added

- Added bounded Photoshop IRB action-descriptor item parsing for simple
  complete item bodies (`bool`, `long`, `doub`, `UntF`, and `TEXT`), including
  item key, type, parsed-count, and truncation fields.
- Added BMFF primary-item metadata carrier flags for known primary C2PA/JUMBF
  metadata items.
- Added semantic query aliases for Canon ColorData source color-transform
  tables and NikonSettings source-processing tables.

### Changed

- Updated public interpretation and API status docs for the expanded
  descriptor-body, BMFF primary-carrier, and MakerNote query coverage.

## 0.4.48 - 2026-06-10

Changes compared with `0.4.47`.

### Added

- Added BMFF primary-linked item semantic rollup counters so HEIF/AVIF/CR3-like
  item graphs expose aggregate metadata, auxiliary, derived, thumbnail, and
  content-description counts next to per-item roles.
- Added bounded Photoshop IRB action-descriptor body header parsing for
  descriptor class name, class ID, and item count when the descriptor payload is
  complete and ASCII/UTF-8-safe.
- Added `MetadataRawDataDescriptor::has_plane_index`, `plane_index`, and
  `requires_primary_raw_plane` so hosts and decoders can prevent primary-plane
  curve/LUT metadata from being treated as active for unrelated stored planes.
- Added XMP `DateTimeDigitized` promotion into the cross-family `Digitized`
  date/time concept.
- Added Canon AF micro-adjustment and ambience-selection aliases to semantic
  query classification for lens-correction and source-processing workflows.

### Changed

- RAW applicability and transfer diagnostics now mark curve/LUT-like metadata
  not applicable when the supplied descriptor says the active raw data is a
  non-primary plane and the metadata requires the primary raw plane.
- Updated public interpretation, API stability, RAW-read, writer-contract, host
  integration, and quick-start docs for descriptor plane binding and the new
  BMFF/IRB/query interpretation coverage.

## 0.4.47 - 2026-06-04

Changes compared with `0.4.46`.

### Added

- Added BMFF primary-item display-dimension and transform-summary fields from
  decoded `ispe`/`irot`/`imir` properties.
- Added broader Photoshop IRB descriptor-header test coverage for timeline,
  sheet-disclosure, onion-skin, count, print-info/style, and origin-path
  resources.
- Added `MetadataRawDataDescriptor::requires_compressed_raw_encoding` so hosts
  can mark curve/LUT-like RAW metadata as active only for compressed RAW
  storage.
- Added XMP GPS date/time composite interpretation from `GPSDateStamp` plus
  `GPSTimeStamp`, matching the existing EXIF GPS timestamp composition model.

### Changed

- RAW applicability and transfer diagnostics now drop compressed-only RAW
  curve/LUT candidates when the supplied storage descriptor is uncompressed or
  packed, while keeping black/white-level and storage facts applicable.
- Updated public interpretation, RAW-read, writer-contract, and API stability
  docs for the new descriptor flag and expanded BMFF/IRB/GPS interpretation
  surface.

## 0.4.46 - 2026-06-04

Changes compared with `0.4.45`.

### Added

- Added BMFF `ipma.<property-type>.*` association summary fields for common
  item properties, including association, primary-association, and essential
  counts where present.
- Added Photoshop IRB descriptor-header interpretation for `HDRToningInfo` and
  `PrintInfo`.
- Added `PrepareTransferRequest::source_raw_data_descriptor` and the
  `RawDataDescriptorFiltered` policy reason so compatible-file preparation can
  still drop RAW-processing metadata when the source pixels are rendered.
- Added optional Python transfer helper keyword `source_raw_data_descriptor`
  for the same prepare-time RAW-processing filter.
- Added IPTC date/time promotion into cross-family `Created` concept
  candidates while preserving the IPTC-specific `DateCreated` candidate.
- Added bounded Nikon Active D-Lighting value labels for Low, Normal, and High.

### Changed

- Updated public interpretation, support, RAW-read, writer-contract, and API
  stability docs for descriptor-backed prepare filtering and the expanded
  BMFF/Photoshop interpretation subset.

## 0.4.45 - 2026-06-01

Changes compared with `0.4.44`.

### Added

- Added descriptor-aware transfer concept diagnostic overloads so hosts can
  pass `MetadataRawDataDescriptor` directly into transfer preflight and get
  keep/drop decisions that reflect rendered-vs-stored-RAW applicability.
- Added thin Python `Document` and `TransferSourceSnapshot`
  `transfer_concept_diagnostics(...)` overloads that accept
  `MetadataRawDataDescriptor`.

### Changed

- RAW-processing diagnostics whose supplied descriptor marks the data as
  rendered now drop as `raw_applicability_not_applicable`, even under
  compatible-file safety.

## 0.4.44 - 2026-06-01

Changes compared with `0.4.43`.

### Added

- Added BMFF `ipco` property-container summary fields, including total,
  known, unknown, and per-property-type counts for the bounded primary-property
  model.
- Added RAW data descriptor overloads for concept resolution so hosts can mark
  RAW-processing candidates as stored-raw applicable, encoding-conditional, or
  not applicable for rendered data.
- Added transfer-diagnostic RAW applicability fields and reason tokens for
  conditional or not-applicable RAW curve/linearity metadata.
- Added Photoshop IRB `IPTCDataBytes` interpretation for embedded IPTC-NAA
  resources while preserving the raw IRB payload.

### Changed

- Updated public API, interpretation, support, and RAW-read planning docs for
  descriptor-bound RAW applicability and BMFF/Photoshop interpretation growth.

## 0.4.43 - 2026-05-30

Changes compared with `0.4.42`.

### Added

- Added bounded BMFF `iloc`/`idat` item-data layout summaries, including item
  construction methods, extent counts, total extent byte counts, idat-backed
  item counts, and primary-item location aliases.
- Added Photoshop IRB `LightroomWorkflow` ASCII text interpretation while still
  preserving the raw IRB resource.
- Added focused coverage for Nikon Z6 III per-version MakerNote menu-setting
  value-name dispatch.
- Added experimental RAW data encoding and RAW applicability state tokens for
  cross-family concept candidates, exposed through C++ and the thin Python
  dictionary wrappers.

### Changed

- Updated public interpretation, support, API-stability, and RAW-read planning
  docs to describe BMFF item-data layout fields and the new conservative RAW
  curve applicability scaffold.

## 0.4.42 - 2026-05-29

Changes compared with `0.4.41`.

### Added

- Added bounded BMFF `grpl` item-group interpretation for HEIF/AVIF/CR3-style
  metadata graphs, exposing group type/id rows, entity counts, entity ids,
  per-type summaries, and primary-item group memberships.
- Added focused BMFF coverage for alternate and stereo item groups, including
  primary membership and per-group-type derived fields.

### Changed

- Updated public interpretation and RAW-read parity docs to reflect the new
  BMFF item-group graph surface.
- Added a public planning note that RAW curve/LUT metadata should eventually be
  bound to raw image data descriptors before being interpreted as active for a
  specific storage or compression mode.

## 0.4.41 - 2026-05-28

Changes compared with `0.4.40`.

### Added

- Added dedicated semantic query and concept roles for RAW value curves, RAW
  linearity limits, RAW calibration curves, and RAW curve control points.
- Added conservative RAW curve classification for DNG linearization/linearity
  tags plus selected Sony, Nikon, Kodak, Panasonic, and Phase One/Leaf-style
  curve or calibration metadata names.
- Added rendered-transfer diagnostics that drop source RAW curve/linearity
  metadata while keeping it eligible for compatible RAW/DNG-style transfers.
- Added C++ and Python enum exposure plus focused query, interpretation,
  concept-resolution, and transfer-diagnostic tests for the new RAW curve
  roles.

### Changed

- Updated public interpretation, API, host-integration, development, quick
  start, and writer-contract docs to describe source-bound RAW curve metadata
  and rendered-image transfer behavior.

## 0.4.40 - 2026-05-27

Changes compared with `0.4.39`.

### Added

- Added bounded Canon MakerNote numeric labels for additional sub-IFDs,
  including focal length, AFInfo2, aspect, file info, processing, lighting
  optimization, vignetting correction, time info, filter, HDR, and selected
  CanonCustom function fields.
- Added bounded Nikon MakerNote numeric labels for selected main, AFInfo,
  AFInfo2, ISOInfo, HDRInfo, VRInfo, FlashInfo, PictureControl, multi-exposure,
  world-time, distortion, AF-tune, lens-data, location, and NikonSettings
  scalar fields.
- Added bounded Pentax MakerNote numeric labels for additional main scalar
  fields plus LensCorr, AWBInfo, SRInfo2, LensRec, and TimeInfo sub-IFDs.
- Added bounded Olympus MakerNote numeric labels for main, CameraSettings,
  FocusInfo, Equipment, RawDevelopment, and ImageProcessing scalar fields.
- Added bounded Casio MakerNote numeric labels for common Type2 and legacy
  scalar fields.
- Added bounded Panasonic MakerNote numeric labels for the remaining stable
  long-tail main-table scalar fields.
- Added bounded Fujifilm residual numeric labels for saturation, contrast,
  noise reduction, film mode, dynamic-range setting, shutter type, image
  generation, scene recognition, and face element-type fields.
- Added focused C++ coverage for the expanded Canon, Nikon, Fujifilm, Pentax,
  Olympus, Casio, and Panasonic MakerNote value-name dispatch paths.

### Fixed

- Kept ambiguous per-model or version-dependent MakerNote values unlabeled
  instead of emitting plausible but wrong text for Nikon, CanonCustom, Olympus,
  Pentax, Casio, and Panasonic edge cases.

## 0.4.39 - 2026-05-27

Changes compared with `0.4.38`.

### Added

- Added bounded Canon MakerNote numeric labels for common CameraSettings,
  ShotInfo, main, and MyColors scalar fields such as quality, drive, zoom,
  contrast, saturation, focus range, image size, AE mode, white balance, slow
  shutter, bracketing, control mode, ND filter, date stamp, and MyColors mode.
- Added bounded Fujifilm MakerNote numeric labels for decoded real-file
  `mk_fuji*` contexts, including sharpness, white balance, macro, slow sync,
  auto bracketing, blur warning, color mode, and dynamic range fields.
- Added bounded Panasonic MakerNote numeric labels for image quality, image
  stabilization, audio, color effect, noise reduction, rotation, AF assist,
  conversion lens, travel day, battery, text stamp, and related scalar fields.

### Fixed

- Fixed Fujifilm value-name dispatch for decoded `mk_fuji*` IFD tokens.
- Stopped emitting ambiguous Ricoh `0x1003` and Minolta main `0x0103` labels
  where the meaning depends on value type, model, or tag-name context that is
  not available to the numeric value-name helper.
- Tightened context-sensitive Panasonic contrast, Ricoh noise-reduction, and
  Sony Tag2010B quality labels so ambiguous values remain unlabeled instead of
  receiving a wrong stable label.

## 0.4.38 - 2026-05-26

Changes compared with `0.4.37`.

### Added

- Added bounded Pentax MakerNote numeric labels for selected main, AEInfo,
  FlashInfo, and Type2 exposure, metering, focus, flash, white-balance, and
  picture/recording mode fields.
- Added Pentax MakerNote compat tag names for the selected main, AEInfo,
  FlashInfo, and Type2 contexts used by the new value labels.
- Added bounded Olympus MakerNote numeric labels for selected CameraSettings,
  RawDevelopment, RawDevelopment2, and ImageProcessing exposure, metering,
  focus, flash, white-balance, scene, picture, and multiple-exposure fields.
- Added bounded Panasonic MakerNote numeric labels for selected main and
  subdirectory white-balance, focus, macro, shooting, burst, film, flash,
  intelligent-exposure, multi-exposure, video-burst, scene, and dark-focus
  fields.
- Added bounded Phase One/Leaf-style MakerNote numeric labels for camera
  orientation, RAW format, and sequence-kind fields.
- Added bounded Kodak, Minolta, Sigma, Samsung, and Ricoh MakerNote numeric
  labels for selected exposure, metering, focus, flash, white-balance, scene,
  style, drive, quality, color, and image-effect fields.
- Added focused C++ coverage for Pentax/Olympus/Panasonic value-name dispatch
  and Pentax/Olympus exposure-label concept promotion.
- Added focused C++ coverage for Phase One/Leaf, Kodak, Minolta, Sigma,
  Samsung, and Ricoh value-name dispatch plus Ricoh exposure-label concept
  promotion.
- Added `AEProgramMode` exposure-query recognition so compatible MakerNote
  auto-exposure program fields can flow into exposure concept candidates.

## 0.4.37 - 2026-05-26

Changes compared with `0.4.36`.

### Added

- Added bounded Fujifilm MakerNote numeric labels for flash mode, focus mode,
  AF mode, picture mode, multiple exposure, and focus/exposure warning fields.
- Added bounded Sony MakerNote numeric labels for selected main,
  CameraSettings, CameraSettings2, CameraSettings3, MoreSettings, and Tag2010
  exposure, metering, focus, flash, drive, and release-mode fields.
- Added focused C++ coverage for Sony/Fujifilm value-name dispatch and Sony
  MakerNote exposure-label concept promotion.

## 0.4.36 - 2026-05-26

Changes compared with `0.4.35`.

### Added

- Added standard EXIF `ExposureMode` numeric labels and dispatch through
  `exif_tag_numeric_value_name(...)`.
- Added Canon MakerNote camera-info flash-metering labels for bounded
  `FlashMeteringMode` contexts.
- Added bounded Nikon MakerNote numeric labels for main `FlashMode`, selected
  Z-series metering/focus/multiple-exposure contexts, and D500/D810 metering
  contexts.
- Added C++ and Python API coverage for the new value-name helper surface.

## 0.4.35 - 2026-05-26

Changes compared with `0.4.34`.

### Added

- Added BMFF brand-name fields for `ftyp.major_brand` and compatible brands,
  plus an explicit `ftyp.compat_brand_count`.
- Added BMFF item-semantic aggregate counters for known, metadata, image,
  EXIF, XMP, JUMBF, C2PA, ICC profile, auxiliary, derived, thumbnail,
  content-description, URI, and JSON item roles.
- Added bounded Photoshop IRB `XMLData` ASCII text interpretation while keeping
  the raw resource entry lossless.
- Added Canon MakerNote camera-setting numeric labels for common flash, focus,
  metering, exposure-mode, and spot-metering fields through the existing
  `exif_tag_numeric_value_name(...)` helper. Exposure concept candidates now
  reuse those labels when the decoded vendor field has a safe enum mapping.

## 0.4.34 - 2026-05-25

Changes compared with `0.4.33`.

### Added

- Added bounded Photoshop IRB interpretation for `Photoshop2Info`,
  `Photoshop2ColorTable`, `RawImageMode`, `SpotHalftone`, `JumpToXPEP`,
  `AutoSaveFilePath`, `AutoSaveFormat`, ImageReady variable/data-set text,
  legacy halftone/transfer/duotone/EPS byte summaries, print-flag byte fields,
  and path-resource record counts/selectors.
- Added focused short-payload regressions for the new Photoshop IRB structured
  fields.
- Added a self-contained `metaread` Photoshop IRB CLI smoke and included it in
  the CLI release gate.

### Changed

- Public support/status docs now separate Photoshop IRB interpreted resources,
  descriptor-header summaries, record summaries, byte-count summaries, and
  raw-only resources.

## 0.4.33 - 2026-05-25

Changes compared with `0.4.32`.

### Added

- Added bounded Photoshop IRB interpretation for `BorderInformation`,
  `BackgroundColor`, and two-byte `EffectiveBW` payloads.
- Added Photoshop IRB color-sampler header and record interpretation for
  `ColorSamplersResource` and `ColorSamplersResource2`.
- Added descriptor-version and descriptor-byte-count summaries for descriptor
  backed Photoshop IRB resources such as `LayerComps`, `MeasurementScale`,
  `TimelineInfo`, `PrintInfo2`, `PrintStyle`, and `PathSelectionState`.

### Changed

- Public interpretation/support docs now describe the expanded Photoshop IRB
  fixed-layout and descriptor-header subset.

## 0.4.32 - 2026-05-25

Changes compared with `0.4.31`.

### Added

- Added Photoshop IRB `DisplayInfo` and `GridGuidesInfo` fixed-layout
  interpretation for display colors, opacity/kind, grid cycles, guide
  locations, and guide directions.
- Added JUMBF box-label emission from parsed `jumd` boxes as structural
  `JumbfField` entries.

### Changed

- Public interpretation/support docs now describe the expanded Photoshop IRB
  fixed-layout subset and JUMBF label visibility.

## 0.4.31 - 2026-05-25

Changes compared with `0.4.30`.

### Added

- Added bounded BMFF `ipma` association interpretation, exposing item id,
  property index, essential flag, and known property type/name rows for
  inspected item-property links.
- Added Photoshop IRB thumbnail-header interpretation for resource ids
  `0x0409` and `0x040C`, exposing format, dimensions, byte counts,
  bit depth, planes, and payload data size without decoding thumbnail pixels.

### Changed

- Public interpretation/support docs now describe the expanded BMFF
  item-property association surface and Photoshop IRB thumbnail headers.

## 0.4.30 - 2026-05-24

Changes compared with `0.4.29`.

### Added

- Added bounded BMFF primary item-property interpretation for `pasp`, `pixi`,
  and `clap`, exposing primary pixel aspect ratio, pixel component bit depth,
  and clean-aperture rationals as derived `BmffField` entries.
- Added Photoshop IRB derived byte-count fields for embedded ICC, EXIF,
  EXIF2, and XMP resource payloads.
- Added optional Photoshop IRB embedded XMP and ICC decode paths, with result
  counters and focused regression tests.

### Changed

- Public interpretation/support docs now describe the expanded BMFF item
  property surface and the broader Photoshop IRB embedded-carrier handling.

## 0.4.29 - 2026-05-22

Changes compared with `0.4.28`.

### Added

- Added explicit `ComputationalProcessing`, `ThermalProcessing`, and
  `StitchProcessing` semantic query and concept roles for source-bound
  vendor RAW/source-processing metadata.
- Added Python enum exposure and focused query, interpretation, concept, and
  transfer-diagnostic tests for the new source-processing subroles.

### Changed

- Vendor RAW/source-processing query candidates now preserve computational,
  thermal, and stitch/panorama intent instead of collapsing all such fields
  into generic `source_processing`.
- Transfer diagnostics now report role-specific messages for computational,
  thermal, and stitch/panorama source-processing drops.

## 0.4.28 - 2026-05-22

Changes compared with `0.4.27`.

### Added

- Added explicit `SourceColorTransform` query and concept roles for
  source-bound camera RAW profile/look/tone-curve/style metadata.
- Added C++ and Python enum exposure plus focused query, interpretation, and
  concept-transfer-safety tests for source color transforms.

### Changed

- Source-bound color transform concept candidates are now marked
  rendered-image unsafe while remaining visible for compatible-source transfer
  and inspection UI policy decisions.
- Public docs now call out external validation-tool patching expectations for
  untrusted metadata test files.

## 0.4.27 - 2026-05-22

Changes compared with `0.4.26`.

### Added

- Added a distinct `ColorProfile` semantic query role for EXIF color-space
  evidence, ICC header/tag entries, XMP ICC/profile fields, and PNG profile
  text carriers.
- Added query, interpretation, concept-resolution, and Python enum coverage for
  color-profile evidence while preserving source-entry provenance.

### Changed

- Color-profile concept resolution now keeps ICC profile and color-space roles
  separate when query-backed records and direct cross-family candidates are
  merged.
- Public progress docs now describe the color-profile query/interpretation
  surface and keep the current interpretation/query readiness estimates aligned
  with the implemented API.

## 0.4.26 - 2026-05-22

Changes compared with `0.4.25`.

### Added

- Added contextual Canon MakerNote main tag `0x0000` names for the observed
  camcorder and Cinema EOS cohorts.
- Added Nikon ShotInfo D850 tag `0x0024` as `PhotoShootingMenuBank`.

### Changed

- Canon AFInfo2 decoding now retries with a bounded in-MakerNote payload scan
  when the primary Canon offset points at a non-AFInfo2 block.
- Nikon ShotInfo `0x0024` compatibility naming now covers the Z f ShotInfo
  layout used by the interpretation path.

## 0.4.25 - 2026-05-21

Changes compared with `0.4.24`.

### Changed

- Improved Canon CameraInfo PictureStyle interpretation for EOS 7D,
  EOS Kiss X7i, and EOS-1D X model cohorts by selecting the matching
  PSInfo/PSInfo2 table.
- Split Canon MakerNote main tag `0x0038` byte-blob naming by observed
  payload size, preserving the placeholder for short blobs while reporting
  long battery-type payloads as `BatteryType`.

## 0.4.24 - 2026-05-21

Changes compared with `0.4.23`.

### Added

- Added the missing Nikon ShotInfo D300A `ISO2` MakerNote name used by the
  ExifTool-compatible interpretation path.

## 0.4.23 - 2026-05-21

Changes compared with `0.4.22`.

### Added

- Added BMFF item type-name and semantic labels for image, EXIF, XMP, JUMBF,
  C2PA, ICC profile, URI, thumbnail, derived-image, auxiliary, and
  content-description items.
- Added bounded primary BMFF color/property summaries for `colr` `nclx`,
  `nclc`, `rICC`, and `prof` properties, including ICC profile byte counts and
  nclx color fields.

## 0.4.22 - 2026-05-21

Changes compared with `0.4.21`.

### Added

- Expanded Photoshop IRB fixed-layout interpretation for alpha channel names,
  Unicode alpha names, alpha identifiers, Pascal captions, and QuickMask info
  while keeping raw resource preservation unchanged.

## 0.4.21 - 2026-05-21

Changes compared with `0.4.20`.

### Added

- Added `query_descriptive_metadata(...)` for bounded descriptive
  EXIF/IPTC/XMP reconciliation across title/headline,
  description/caption, creator/author, and keywords/subject metadata.
- Structured interpretation now includes descriptive query records, preserving
  source entry provenance for UI and host reconciliation workflows.

## 0.4.20 - 2026-05-21

Changes compared with `0.4.19`.

### Added

- Promoted selected decoded vendor/MakerNote exposure names such as
  exposure time, aperture/f-number, ISO, exposure compensation, and exposure
  program into the same cross-family exposure concept roles used by standard
  EXIF fields.

### Changed

- Exposure/gain query matching now treats public `FNumber` names as aperture
  evidence, so decoded vendor records can be found without depending on the
  standard EXIF tag id.

## 0.4.19 - 2026-05-21

Changes compared with `0.4.18`.

### Added

- Added a public writer/transfer use case for late-bound EXR metadata, where a
  streaming writer reserves a fixed-size attribute before pixel writes and
  patches the value bytes after the image is complete.
- Exposure concept candidates now attach human-readable labels for standard
  EXIF exposure program and gain-control values.

### Changed

- Clarified that late-bound EXR metadata should be treated as a future bounded
  fixed-size patch API, not as a general variable-length EXR header rewrite.

## 0.4.18 - 2026-05-20

Changes compared with `0.4.17`.

### Added

- Added an experimental exposure/gain concept resolver that maps
  query-backed exposure records into exposure time, aperture, ISO sensitivity,
  exposure bias, exposure program, gain, and raw exposure-adjustment roles.
- Added conservative transfer hints for exposure concepts: capture exposure
  facts remain safe, while raw/DNG exposure adjustment fields are marked
  unsafe for rendered-image transfer.
- Exposed the new exposure concept and roles through the Python bindings.

### Changed

- Updated public interpretation and host-integration documentation for
  exposure/gain concept coverage and rendered-transfer safety behavior.

## 0.4.17 - 2026-05-20

Changes compared with `0.4.16`.

### Added

- Expanded public vendor RAW/source-processing aliases for long-tail source
  color transforms, white-balance gains, lens/optical correction, creative
  style, picture style, film simulation, dynamic-range, and raw-development
  records.
- Added query and concept coverage showing Samsung color matrix, white-balance,
  lens-correction, and Sony style/source-processing records flow into grouped
  interpretation candidates with conservative transfer hints.

### Changed

- Updated public interpretation/query status to reflect the broader grouped
  alias coverage while keeping rendered-transfer behavior conservative.

## 0.4.16 - 2026-05-20

Changes compared with `0.4.15`.

### Added

- Added stricter grouped color, white-balance, and lens-correction query
  candidates so matrix sets, vector sets, and lens-correction tables only form
  from numeric payloads with safe minimum shapes.
- Added role-specific rendered-transfer diagnostic messages for source color
  transforms, white balance, and lens correction.

### Changed

- Updated public interpretation and query documentation to describe grouped
  color/white-balance/lens records as shape-checked inspection/source-bound
  data.

## 0.4.15 - 2026-05-19

Changes compared with `0.4.14`.

### Added

- Added normalized vendor RAW geometry candidates for Canon aspect/crop
  metadata, Nikon Capture crop bounds, and Sony panorama crop margins.
- Extended concept and transfer-diagnostic tests so these vendor geometry
  records remain target-image-spec-gated for rendered transfers.

## 0.4.14 - 2026-05-19

Changes compared with `0.4.13`.

### Added

- Added normalized Fujifilm RAF raw crop and raw zoom interpretation from
  full-size, top-left, and cropped-size fields into active-area/crop
  rectangles with margins when the full sensor size is available.
- Added concept and transfer-diagnostic coverage showing vendor-derived
  geometry remains target-owned for rendered transfers while grouped color,
  white-balance, lens-correction, and source-processing records remain
  inspection/source-bound data.

## 0.4.13 - 2026-05-18

Changes compared with `0.4.12`.

### Added

- Added per-family grouped semantic query candidates for vendor MakerNote/RAW
  white-balance, color, raw-storage, sensor, and source-processing fields so
  structured interpretation and concept resolution can expose table/vector/set
  records instead of only per-entry bucket matches.

## 0.4.12 - 2026-05-18

Changes compared with `0.4.11`.

### Added

- Added stable severity and message helpers for transfer concept diagnostics so
  host/UI code can display concise transfer-preview reasons without inventing
  its own default text.

### Changed

- Expanded conservative RAW/source-processing classification for additional
  Pentax, Panasonic, Olympus, Kodak, Minolta, and Sigma MakerNote/raw table
  patterns.

## 0.4.11 - 2026-05-15

Changes compared with `0.4.10`.

### Added

- Added concept-based transfer diagnostics so host/UI code can preflight
  concept candidates as kept, dropped, requiring target image specs, or
  conflict-bearing under compatible-file or rendered-image safety modes.
- Added GPS altitude-reference presentation helpers for concept candidates.

### Changed

- Expanded transfer-critical MakerNote/source-processing classification for
  Nikon, Canon, Sony, Fujifilm, and Phase One/Leaf-style raw-processing terms.

## 0.4.10 - 2026-05-15

Changes compared with `0.4.9`.

### Added

- Added host-facing transfer hints to metadata concept candidates so callers
  can distinguish generally safe concepts from source-bound, rendered-unsafe,
  and target-image-spec-dependent metadata before transfer.
- Python concept dictionaries now expose the same transfer hint, compatible-file
  safety, rendered-image safety, source-bound, and target-image-spec fields.
- Added focused tests for concept transfer hints, color/geometry conflict
  reporting, compatible-file preservation, rendered-image filtering, and
  transfer-critical MakerNote classification.

### Changed

- Expanded transfer-critical MakerNote/source-processing classification for
  RAW crop/active-area/border names, source color transforms and WB terms, lens
  correction/shading/distortion terms, raw black/white/linearization terms, and
  multi-frame/computational capture state.
- Updated the public interpretation and host-integration docs to describe the
  concept transfer hints and current interpretation readiness.

## 0.4.9 - 2026-04-28

Changes compared with `0.4.8`.

### Added

- Added Phase One/Leaf RAW geometry helpers that normalize decoded
  `SensorWidth`, `SensorHeight`, `SensorLeftMargin`, `SensorTopMargin`,
  `ImageWidth`, and `ImageHeight` fields into an active raw rectangle plus
  right/bottom margins.
- Added `phaseone_raw_processing_from_store()` to expose normalized Phase
  One/Leaf RAW color matrices, white-balance levels, black level, sensor
  temperatures, raw-data/storage sizes, and sensor-calibration summaries.
- Python `Document` and `TransferSourceSnapshot` now expose
  `phaseone_raw_geometry()` and `phaseone_raw_processing()` thin wrappers over
  the same C++ helpers.
- Added `vendor_raw_processing_from_store()` and
  `classify_vendor_raw_processing_field()` for conservative Sony, Canon,
  Nikon, Fujifilm, Pentax, Panasonic, Olympus, Kodak, Minolta, Sigma, Samsung,
  Ricoh, Apple, DJI, Google, FLIR, Casio, Sanyo, KyoceraRaw, Reconyx, HP, JVC,
  GE, Motorola, Nintendo, and Microsoft RAW/source-processing field summaries,
  including a vendor-private RAW table bucket for private or unknown table
  entries.
- Python `Document` and `TransferSourceSnapshot` now expose
  `vendor_raw_processing(family)` for the same grouped
  Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/
  Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/Reconyx/HP/JVC/
  GE/Motorola/Nintendo/Microsoft summaries.
- Added `transfer_safety_audit_from_store()` plus Python
  `transfer_safety_audit()` methods so hosts can preflight rendered-image
  drops by source group, filtered count, C2PA invalidation count, and
  Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/
  Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/Reconyx/HP/JVC/
  GE/Motorola/Nintendo/Microsoft RAW/source-processing bucket.
- Added the first experimental semantic metadata query API in
  `openmeta/metadata_query.h`, with `query_crop_metadata(...)` returning both
  raw matches and normalized crop/active-area candidates for DNG crop tags,
  `ActiveArea`, Phase One/Leaf RAW geometry, and fuzzy crop/border-style XMP
  property paths.
- Extended the experimental semantic metadata query API with focused helpers
  for exposure/gain, white balance, color, lens correction, and orientation
  metadata. Non-crop queries return deterministic per-entry candidates with
  numeric value extraction when values are scalar or bounded numeric arrays.
- Added native Fujifilm RAF read coverage for header-declared FujiIFD/TIFF
  offsets, RAF header fields, RAF directory geometry tags, and RAFData geometry
  projection.
- RAF scanning now follows the header-declared preview JPEG metadata before the
  FujiIFD/raw section, so standard preview-carried EXIF tags are decoded
  together with native RAF fields.
- Rendered-image transfer safety now treats decoded native RAF header/directory
  fields as Fujifilm RAW/source-processing metadata and drops them before
  rendered-target serialization.
- BMFF direct emit package planning/writing now accepts both metadata item
  routes and bounded ICC `colr/prof` property routes, and the package replay
  path is regression-covered for foreign-`meta` graph merges.
- Added native Sigma X3F read coverage for common header fields, header
  extension adjustment fields, known `PROP` properties, and section-directory
  JPEG metadata discovery while preserving the older embedded-EXIF fallback.
- Added grouped semantic-query candidates for DNG color matrix/calibration/
  reduction/forward matrix sets, DNG white-balance vector sets, and
  lens-correction table groups. Candidate value-shape labels now include
  `vector_set`, `matrix_set`, and `table`.
- Added `query_raw_processing_metadata(...)` plus Python wrappers for
  conservative RAW-processing queries covering black/white levels,
  linearization tables, CFA/sensor layout, source geometry, and raw-storage
  identifiers.
- Python `Document` and `TransferSourceSnapshot` now expose thin wrappers for
  the experimental semantic metadata query API, including generic
  `metadata_query(kind)` and focused query helpers.
- Added opt-in raw-carrier preservation for `TransferSourceSnapshot` reads,
  including original container block ranges, route hints, bounded payload
  bytes, snapshot-local decoded entry links, C++ result counters, and thin
  Python accessors. Transfer execution still uses decoded metadata by default.
- Added `raw_carrier_passthrough_audit_from_snapshot()` plus Python
  `TransferSourceSnapshot.raw_carrier_passthrough_audit()` so hosts can
  preflight opt-in raw carriers before any host-owned passthrough decision.
  The audit reports candidate carriers and primary block reasons including
  missing payloads, target incompatibility, safety filtering, content-bound
  C2PA, profile policy, missing decoded entry links, and unsupported carrier
  kinds.
- Added `TransferRawCarrierPassthroughMode::WhenSafe` for snapshot-based
  preparation, plus the matching Python enum and snapshot transfer keyword.
  The first bounded path preserves eligible non-C2PA JUMBF and OpenMeta draft
  unsigned C2PA invalidation carriers for JPEG, JXL, and BMFF targets, plus
  draft unsigned C2PA invalidation carriers for WebP. EXIF/XMP/ICC/IPTC
  transfer remains decoded re-emission.
- Added optional `OPENMETA_ENABLE_RAPIDFUZZ` support for RapidFuzz-backed
  semantic-query XMP/property-path matching, plus
  `metadata_query_fuzzy_search_available()` so tools can detect whether the
  stronger fuzzy matcher is compiled in.
- Semantic-query matches now report `exact_match`, `fuzzy_match`, and
  `fuzzy_score` so host UI and Python tools can distinguish exact tag/name
  hits from RapidFuzz near-miss results.
- Added `openmeta/orientation.h` with stable EXIF/TIFF orientation
  interpretation helpers for user-facing labels, clockwise rotation degrees,
  mirrored-state checks, width/height-swap checks, and rotation-only fallbacks,
  plus matching thin Python wrappers.
- Added `openmeta/exif_value_names.h` with stable labels for common
  EXIF/TIFF/DNG enum-like numeric values, plus matching thin Python wrappers.
- Added `openmeta/metadata_interpretation.h` with query-backed structured
  interpretation records for host/UI code that wants normalized semantic
  records instead of raw query candidates.
- Added `openmeta/metadata_concepts.h` with first bounded cross-family concept
  resolution for orientation, date/time, color/profile, and GPS candidates
  across EXIF, XMP, IPTC, ICC, and PNG text where applicable, including source
  families, candidate source entries, preferred entries, normalized compare
  keys, parsed date/time fields, timezone/precision classification, combined
  GPS date/time candidates, GPS altitude-reference state, and same-role
  conflict flags.
- Extended cross-family concept resolution with geometry candidates for crop,
  active area, border, and sensor-geometry records. Geometry candidates expose
  canonical origin, size, rect, and margin fields when the query/interpretation
  layer can normalize them.
- Extended cross-family concept resolution with full normalized value vectors
  and new lens-correction and RAW-processing concept families. Color/white
  balance, lens-correction, black/white level, linearization, CFA layout,
  raw-storage, and source-processing concepts now preserve grouped
  interpretation values instead of only the first scalar preview values.
- GPS concept conflict checks now compare numeric coordinates and altitude with
  explicit tolerances, while grouped candidates that share source entries are
  treated as alternate views of the same evidence rather than conflicts.
- Semantic crop queries now expose canonical border-margin candidates for
  parseable border/padding XMP text, DNG masked-area candidates, and Phase
  One/Leaf geometry margins.
- Vendor RAW/source-processing classification now distinguishes source-private
  preview, face-geometry, computational, thermal, and stitch/panorama buckets
  in addition to the existing color, white-balance, geometry, storage,
  lens-correction, raw-data, sensor, and private-table groups.
- Vendor RAW/source-processing classification now also treats common
  computational MakerNote terms such as pixel-shift, multi-shot, composite, and
  auto-lighting optimizer fields as source-private processing metadata for
  audit and rendered-transfer safety decisions.
- Added focused regression coverage for compatible-file versus rendered-image
  transfer safety: compatible mode keeps serializable source RAW/camera-specific
  metadata, while rendered mode drops source-specific metadata and uses
  host-provided target image specs.
- Configured BMFF image-usability checks now infer target-owned image specs
  before transfer, so real HEIF/AVIF/CR3 targets can exercise EXIF
  image-property, MakerNote, ICC, and XMP transfer paths without using the
  synthetic fixture geometry.
- Added a public RAW read-parity plan that tracks camera RAW family gaps
  against ExifTool-style coverage without broadening writer guarantees.
- Added a public interpretation status matrix that separates decode visibility
  from semantic names, query shapes, transfer-safety classification, and
  competitor-facing interpretation gaps.
- Added read-path coverage for EXIF/TIFF-carried ICC profiles and IPTC-IIM
  payloads, bare JPEG APP1 XMP packets, and XMP packets that use alternate
  `xmpmeta` namespace prefixes.

### Changed

- Added `OPENMETA_TEST_RUNTIME_LIBRARY_PATH` so CTest-launched external
  validation tools can run with a matching non-default C++ runtime lookup path,
  and documented the `libc++` test-prefix workflow.
- Empty `rdf:about=""` XMP description attributes are now ignored, matching
  common tool behavior, while non-empty `rdf:about` values and empty RDF
  collections such as empty `dc:subject` bags remain decoded.
- The `openmeta_wheel` CMake target now forwards the active compiler flags,
  Python selection, `OPENMETA_USE_LIBCXX`, and optional-feature defines into
  the nested scikit-build wheel configure step and shares the install-time
  wheel script, instead of relying only on environment variables.
- Phase One-family IIQ MakerNote detection now recognizes Leaf/Credo-style
  files as Phase One MakerNotes before the generic Kodak `IIII` fallback.
- Rendered-image transfer safety now treats Phase One/Leaf RAW sensor geometry,
  color matrices, white-balance coefficients, raw-data/storage fields,
  black-level fields, and sensor-calibration tables as raw-specific metadata
  and drops them for rendered outputs.
- Rendered-image transfer safety now also filters decoded Sony, Canon, Nikon,
  Fujifilm, Pentax, Panasonic, Olympus, Kodak, Minolta, Sigma, Samsung, Ricoh,
  Apple, DJI, Google, FLIR, Casio, Sanyo, KyoceraRaw, Reconyx, HP, JVC, GE,
  Motorola, Nintendo, and Microsoft MakerNote fields classified as source RAW,
  computational, thermal, color/WB, geometry/storage, raw-data, sensor,
  lens-correction, preview/face geometry, stitch/panorama geometry, or
  vendor-private table metadata.
- Live-vendor RAW/source-processing classification now covers additional Apple
  computational capture/HDR/motion fields, DJI pose and thermal fields, Google
  shot-log metering fields, and FLIR radiometric/raw-value/geometry fields.
- Portable XMP output now recognizes Adobe DNG XMP properties (`dng:*`) as a
  known namespace, so compatible-file transfer can serialize retained DNG
  profile metadata while rendered-image safety still filters it as source raw
  calibration metadata.
- Prepared EXIF transfer now reports decoded-only vendor MakerNote sub-IFDs as
  non-serializable writer inputs instead of implying that OpenMeta can
  reconstruct vendor MakerNote blobs. The original raw `ExifIFD:MakerNote`
  payload is still preserved when available.
- Rendered-image transfer safety now uses the current DNG tag numbers for
  source-bound profile/gain tables, raw digests/storage identifiers,
  forward matrices, and opcode/correction lists.
- `metaread` and `python -m openmeta.python.metaread` now print compact Phase
  One RAW geometry/processing summaries when those decoded fields are present.
- Draft C2PA verification now exposes opt-in trusted certificate-chain
  enforcement through `ValidateOptions`, `metavalidate`, `metadump`, and Python
  `read()`/`validate()` wrappers. Without this option, signature status and
  chain-trust detail remain separate signals; with it, untrusted or missing
  chains fail verification instead of reporting a loose `verified` result.
- `metaread` and `python -m openmeta.python.metaread` now print compact
  Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/
  Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/Reconyx/HP/JVC/
  GE/Motorola/Nintendo/Microsoft `vendor_raw_processing[...]` summaries when
  matching decoded fields are present.
- BMFF foreign-`meta` insertion now upgrades supported `iloc` version 0/1
  graphs to output `iloc` version 2 when inserted metadata needs 32-bit item
  IDs, removing the previous requirement that the target already used
  `iloc` version 2.
- BMFF foreign-`meta` insertion now treats retained `iloc` construction method
  1 records as supported when they point into an existing `idat` with data
  reference index 0, and rejects external data references or unsupported
  construction methods safely.
- BMFF foreign-`meta` insertion now preserves retained `iloc` construction
  method 2 item-reference extents when their `iref` `iloc` references are
  parseable and every referenced item remains retained with a supported local
  location.
- BMFF ICC property replacement now preserves the prior ICC association scope
  across retained items instead of collapsing replacement to only the primary
  item, and keeps the prior essential association bit on replacement
  associations.

### Fixed

- Fixed standalone EXIF/TIFF recovery when a file has a short non-TIFF prefix
  or malformed JPEG prefix before the `Exif` preamble.
- Fixed draft C2PA verification status handling for malformed COSE_Sign1 byte
  arrays, unresolved explicit detached-payload references, and nested numeric
  claim references with conflicting label/URI fields.
- Fixed appended metadata-only BMFF `meta` boxes to advertise inserted item
  payloads through file-offset `iloc` records, allowing CR3-style targets to
  expose appended EXIF/XMP metadata through ExifTool-compatible readers.
- Fixed sidecar-only BMFF transfer so existing OpenMeta-written XMP items are
  preserved when the prior metadata-only `meta` box uses file-offset `iloc`
  records.
- High-level C2PA validation now emits a warning when a signature verifies but
  the certificate chain is not trusted unless strict trusted-chain enforcement
  is enabled.
- Prepared-transfer payload/package artifact deserializers now reject truncated
  oversized entry counts before reserving output vectors.

### Tests And Validation

- Added public synthetic coverage for Leaf/Credo IIQ MakerNote detection and
  normalized Phase One RAW geometry and raw-processing helper behavior.
- Added public synthetic coverage for semantic crop queries, including DNG
  default crop pairs, DNG `ActiveArea`, Phase One/Leaf RAW geometry, fuzzy XMP
  path matching, deleted-entry filtering, and same-IFD crop pairing.
- Added public synthetic coverage for standard EXIF exposure/gain and
  orientation queries, XMP white-balance matching, DNG color-matrix matching,
  and vendor RAW-processing lens-correction classification reuse.
- Added public synthetic coverage for grouped semantic-query candidates:
  DNG color matrix sets, DNG white-balance vector sets, and vendor
  lens-correction table groups.
- Added public synthetic coverage for standalone EXIF/TIFF recovery after
  unknown-prefix and malformed-JPEG-prefix inputs.
- Added public regression coverage and a libFuzzer target for prepared-transfer
  payload/package artifact deserialization and replay.
- Added public synthetic coverage for
  Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/
  Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/Reconyx/HP/JVC/
  GE/Motorola/Nintendo/Microsoft RAW/source-processing
  classification and grouped summary behavior.
- Extended rendered-image safety coverage to verify Phase One/Leaf RAW
  geometry, color, raw-data/storage, black-level, and sensor-calibration fields
  are counted as raw-specific metadata and filtered from rendered transfers.
- Extended rendered-image safety coverage to verify decoded Sony, Canon, Nikon,
  Fujifilm, Pentax, Panasonic, Olympus, Kodak, Minolta, Sigma, Samsung, Ricoh,
  Apple, DJI, Google, FLIR, Casio, Sanyo, KyoceraRaw, Reconyx, HP, JVC, GE,
  Motorola, Nintendo, and Microsoft source-processing fields are counted as
  raw/source-specific metadata and filtered from rendered transfers, including
  anonymous/private RAW, computational, thermal, preview, face-geometry, and
  stitch/panorama table entries.
- Added rendered-image writer-output coverage for JPEG, TIFF/DNG, PNG, WebP,
  JP2, JXL, HEIF, AVIF, and CR3 metadata rewrites to verify serialized outputs
  omit source RAW calibration, raw digests/gain metadata, vendor private RAW
  tables, MakerNotes, camera raw settings XMP, and source JUMBF while
  preserving safe EXIF/XMP fields.
- Added BMFF transfer coverage for merging metadata into a foreign top-level
  `meta` item table that uses `iinf` version 2.
- Added BMFF transfer coverage that verifies multiple foreign top-level `meta`
  boxes and unsupported foreign `iloc`/`iref` versions fail safely instead of
  producing a partial rewrite.
- Added BMFF relation/property graph coverage that verifies stale `cdsc`
  references are removed when replacing foreign Exif/XMP items, existing `ipma`
  associations are preserved while adding/replacing ICC properties, and
  unsupported foreign `ipma` versions fail safely.
- Added BMFF foreign property-graph rejection coverage for duplicate `ipco`,
  duplicate `ipma`, and `ipma` associations that point past the available
  `ipco` property table.
- Added BMFF high-item-ID `iref` coverage that verifies inserted metadata
  references use version 1 item IDs and existing small-ID references are
  preserved when the relation table is upgraded.
- Added BMFF coverage for upgrading an `iloc` version 1 graph at item ID
  `65535` so inserted metadata uses item ID `65536` and remains readable.
- Added BMFF coverage for preserving retained `idat`-relative item extents
  while inserting metadata with absolute file-offset extents, plus fail-safe
  rejection for missing `idat`, external data references, and unsupported
  construction methods.
- Added BMFF coverage for preserving retained method-2 item-reference extents
  with explicit extent indexes and reference-order fallback, plus fail-safe
  rejection when method-2 references are missing or would point to an item
  removed by metadata replacement.
- Added BMFF coverage for multi-item ICC replacement, verifying secondary items
  that referenced the replaced ICC property are retargeted to the transferred
  ICC property.
- Added BMFF coverage that verifies essential `ipma` association flags are
  preserved when ICC properties are replaced.
- Extended the external image-usability gate so optional configured CR3 targets
  also exercise MakerNote transfer. Rendered-mode checks now compare against
  any pre-existing target MakerNote bytes instead of assuming the target had no
  MakerNotes.

## 0.4.8 - 2026-04-27

Changes compared with `0.4.7`.

### Added

- Added bounded 32-bit BMFF item-id insertion for parseable foreign item graphs
  that already use `iloc` version 2. `iloc` version 0/1 targets remain on the
  existing 16-bit insertion path.
- Added `TransferSafetyMode` on `TransferProfile`. The default
  `CompatibleFile` mode keeps current transfer behavior, while `RenderedImage`
  drops source raw color calibration/correction metadata, camera raw settings
  XMP, source ICC profiles, MakerNotes, and non-C2PA JUMBF data for rendered
  image outputs.
- Added transfer policy decisions for filtered image properties, ICC profiles,
  raw color calibration, and camera raw settings.

### Changed

- Public BMFF writer-contract docs now state the remaining item-id-width limit
  explicitly: 32-bit inserted item IDs require an existing `iloc` v2 graph, and
  unsupported or exhausted item-id spaces fail safely instead of truncating IDs.
- BMFF foreign-`meta` insertion keeps newly inserted metadata item records on
  `iloc` construction method 0 with absolute file-offset extents for broad
  reader compatibility.
- BMFF foreign-`meta` insertion now compacts zero/foldable `iloc` base offsets
  to a zero-width base-offset field when rebuilding supported item graphs,
  preserving absolute self-contained item extents for simpler readers.
- C++ and Python `metatransfer` wrappers now accept
  `--transfer-safety compatible|rendered`.
- Writer-contract docs now include a per-group transfer safety matrix for
  rendered-image exports, including opaque MakerNote handling.

### Fixed

- Fixed BMFF `iinf` version 1 scanning to read its 32-bit entry count, keeping
  writer read-back validation aligned with version 1/2 item tables.

### Tests And Validation

- Added a BMFF API roundtrip test that writes Exif and XMP into an `iloc` v2
  target with high item IDs and scans the result back as one Exif item and one
  XMP item.
- Added focused BMFF coverage that verifies inserted Exif/XMP item records use
  construction method 0 and absolute file-offset extents.
- Extended the BMFF image-usability gate with an explicit ExifTool
  reader-layout regression check for transferred Exif items in HEIF/AVIF/CR3
  targets.
- Extended rendered-image safety coverage to require policy decisions for
  image-layout fields, ICC, RAW/DNG color and correction tags, camera raw
  settings XMP, opaque MakerNotes, and non-C2PA JUMBF data.
- Extended the `metatransfer` smoke gate to verify that
  `--transfer-safety rendered` prints user-visible policy decisions for the
  same safety-filtered groups.
- Extended the Python `metatransfer` smoke gate with the same
  `--transfer-safety rendered` policy-output coverage.

## 0.4.7 - 2026-04-27

Changes compared with `0.4.6`.

### Added

- Added bounded foreign top-level BMFF `meta` merge support for parseable
  HEIF/AVIF/CR3-style item graphs. OpenMeta can now merge prepared
  Exif/XMP/JUMBF/C2PA item metadata into an existing foreign `meta` graph
  instead of appending a second competing `meta` box.
- Added BMFF XMP replacement and strip support for foreign item graphs that
  satisfy the bounded primary-item contract: a single parseable `iinf`,
  `iloc` version 0/1/2, `pitm`, and at most one `idat`.
- Added bounded BMFF ICC property merge support for foreign `meta` graphs.
  Existing ICC `colr/prof` and `colr/rICC` properties are removed from
  `iprp/ipco`, existing `ipma` associations are compacted/remapped, and the
  transferred `colr/prof` property is associated with the primary item.
- Added public CMake cache options for external BMFF usability checks:
  `OPENMETA_BMFF_HEIF_TEST_TARGET`, `OPENMETA_BMFF_AVIF_TEST_TARGET`,
  `OPENMETA_BMFF_CR3_TEST_TARGET`, and `OPENMETA_FFMPEG_EXECUTABLE`.
- Added external image-usability gate coverage for BMFF ICC and XMP transfer
  on configured HEIF/AVIF/CR3 targets when local tools can validate them.

### Changed

- BMFF edit/apply now preserves non-`meta` top-level boxes while rebuilding
  supported foreign `meta` item/property graphs in place.
- BMFF embedded-XMP strip mode no longer requires an OpenMeta-authored
  metadata `meta` box when the foreign graph is parseable and has a valid
  primary-item relationship.
- BMFF summary output and gate checks now cover both `bmff_item mime/xmp` and
  `bmff_property colr/prof` transfer results.
- Public transfer docs now describe the updated BMFF preserve/replace/strip
  contract and the configured-target validation limits.

### Fixed

- Fixed duplicate or stale BMFF ICC associations by replacing prior ICC
  properties and remapping `ipma` instead of adding competing property
  entries.
- Fixed supported foreign BMFF XMP strip/replacement paths that previously
  failed even when the target graph had enough structure for a safe bounded
  rewrite.
- Fixed Nikon MakerNote FlashInfo decoding for `0107`/`0108` and
  `0300`/`0301` layouts by emitting Flash Group A/B/C control-mode fields
  with ExifTool-compatible contextual names.

### Tests And Validation

- Added focused BMFF API tests for foreign Exif/XMP item replacement, XMP
  strip, ICC property replacement, existing `ipma` merge/remap, `iloc`
  rebasing, and fail-safe rejection of unsupported foreign graphs.
- Extended Nikon MakerNote tests for Flash Group A/B/C control-mode decoding.
- Extended the public transfer release and image-usability gates to accept
  configured BMFF target files and optional ffmpeg decode fallback.
- Verified the public release with the unit test suite, transfer release gate,
  external image-usability gate, documentation build, and whitespace checks.
