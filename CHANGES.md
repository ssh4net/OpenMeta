# OpenMeta Changes

## 0.4.127 - 2026-09-02

Changes compared with `0.4.126`.

### Changed

- Bounded foreign BMFF `iprp` rewriting now accepts multiple valid `ipma`
  association boxes and consolidates them into one deterministic table in
  first-seen source order.
- Association merging preserves unrelated item/property links, deduplicates
  repeated property indexes, retains an essential bit if any input association
  marks it, and applies managed-item and ICC-property remapping across the
  combined table.
- Aggregate table, entry, and association limits bound multi-table parsing, and
  indexed item lookup avoids quadratic merging for large valid inputs.

### Tests And Validation

- Added duplicate-table, split-association, and mixed v0/v1 regressions for
  deterministic `ipma` consolidation, essential-bit promotion, ICC insertion,
  aggregate resource limits, and fail-closed rejection of an unsupported
  secondary table version.

## 0.4.126 - 2026-09-01

Changes compared with `0.4.125`.

### Added

- Added target-neutral immutable canonical TIFF/EXIF patch plans with opaque
  plan-scoped handles for exact key occurrences and independently owned mutable
  worker instances.
- Added typed fixed-width scalar, rational, array, byte, and ASCII/UTF-8 patch
  batches with allocation-free execution and canonical payload replay.

### Changed

- Canonical TIFF serialization now records source-backed patch locations during
  preparation without exposing offsets or making regenerated IFD pointers and
  synthetic fields patchable.
- Writing and host-integration documentation now defines the lifecycle,
  container boundary, concurrency rules, supported classic-TIFF types, and
  variable-width/MakerNote limitations of prepared canonical patching.

### Tests And Validation

- Added inline/out-of-line typed patch, decode round-trip, duplicate-occurrence,
  foreign-handle, duplicate-handle, alias, rollback, immutable-plan, independent
  worker, and no-reallocation regressions.
- Extended the installed shared-library consumer to compile and execute the new
  canonical patch contract through exported package targets.

## 0.4.125 - 2026-09-01

Changes compared with `0.4.124`.

### Added

- Added a bounded transactional C++ builder for exact EXIF/TIFF/DNG-style,
  XMP, and IPTC-IIM entries, including borrowed typed values, optional wire
  hints, unknown/private tags, scalar/indexed custom XMP, and public
  `MetaStore::reserve(...)` capacity planning.
- Added detached `validate_entry(...)` and `validate_store(...)` structural and
  initial TIFF/EXIF/GPS/DNG/XMP schema validation with a versioned public
  contract, structured diagnostics, resource bounds, duplicate-singleton
  checks, and optional image/CFA/color context.
- Added deterministic target-neutral `serialize_exif_tiff(...)` measure/write
  for unwrapped little-endian TIFF bytes.

### Changed

- JPEG/TIFF, PNG/WebP, and JP2/JXL/BMFF EXIF preparation now wraps the same
  canonical TIFF payload while retaining existing carrier layouts and
  target-specific time-patch offsets.
- TIFF array emission now normalizes native typed array storage to
  little-endian bytes and compatible TIFF wire hints can preserve BYTE, SBYTE,
  or UNDEFINED payload intent.
- Public creation, host-integration, backend, stability, and development docs
  now distinguish logical creation, exact typed authoring, validation,
  canonical payload serialization, and host-owned container framing.

### Tests And Validation

- Added transactional/deep-copy authoring, custom indexed XMP, schema and
  context failure, wire-hint, deterministic measure/write, decode round-trip,
  carrier-wrapper, and patch-offset regressions.
- Re-ran the existing transfer/edit/handoff compatibility suite across all
  supported writer target families.

## 0.4.124 - 2026-08-31

Changes compared with `0.4.123`.

### Added

- Added bounded transactional reverse translation from exact XMP orientation
  and stored-raster dimensions into canonical native TIFF/EXIF geometry groups.
- Added caller-supplied `TransferTargetImageSpec` reconciliation, standard and
  portable dimension-alias agreement, complete-pair handling, explicit target
  mismatch diagnostics, and thin Python parity.

### Changed

- Image geometry translation now emits `SHORT` orientation and `LONG` IFD0 plus
  ExifIFD dimensions only when edited XMP agrees with the actual target image
  specification; rotated display dimensions are never inferred or swapped.
- Translation readiness is now documented at about 82-87% for the declared
  scope after adding target-bound image geometry.

### Tests And Validation

- Added all-orientation, alias, incomplete-pair, mismatch, conflict,
  replacement, tombstone, resource-limit, and transactional regressions.
- Added native JPEG and TIFF write/decode round trips and extended the thin
  Python metadata-editing smoke gate.

## 0.4.123 - 2026-08-31

Changes compared with `0.4.122`.

### Added

- Added bounded transactional reverse translation from XMP exposure time,
  F-number, ISO, focal length, and exposure compensation into tag-specific
  native EXIF scalar fields.
- Added exact integer parsing for typed, fractional, decimal, and scientific
  rational sources, OpenMeta portable aliases and focal-length units, explicit
  source/conflict policies, dirty tombstone removal, and thin Python parity.

### Changed

- Capture translation now enforces native `RATIONAL`, `SHORT`, and `SRATIONAL`
  types and rejects ambiguous aliases, arrays, invalid denominators,
  out-of-range ISO values, and unrepresentable decimals instead of truncating
  or approximating them.
- Translation readiness is now documented at about 80-85% for the declared
  scope after adding five common capture mappings.

### Tests And Validation

- Added exact conversion, source mode, alias ambiguity, conflict, duplicate,
  tombstone, range, resource-limit, and transactional regressions.
- Added native JPEG and TIFF write/decode round trips for all five capture
  fields and extended the Python metadata-editing smoke gate.

## 0.4.122 - 2026-08-31

Changes compared with `0.4.121`.

### Added

- Added bounded transactional reverse translation from exact `xmp:ModifyDate`,
  `tiff:Make`, `tiff:Model`, and `xmp:CreatorTool` properties into native EXIF
  IFD0 fields.
- Added explicit source-selection and per-group conflict policies, dirty
  tombstone removal, duplicate-source rejection, ASCII and resource limits,
  and a thin Python `Document.translate_technical_metadata(...)` wrapper.

### Changed

- `xmp:ModifyDate` translation preserves timezone and up to nine fractional
  digits through native `OffsetTime` and `SubSecTime` companion tags instead
  of truncating them.
- Technical text translation rejects non-ASCII, embedded-NUL, oversized, and
  ambiguous inputs transactionally rather than emitting invalid TIFF ASCII.

### Tests And Validation

- Added exact namespace, IFD/tag placement, precision, source-mode, conflict,
  duplicate cleanup, tombstone, ASCII, and resource-limit regressions.
- Added native JPEG and TIFF write/decode round trips for translated technical
  EXIF and extended the Python metadata-editing smoke gate.

## 0.4.121 - 2026-08-30

Changes compared with `0.4.120`.

### Fixed

- Preserved semicolon-separated dependency prefix lists when the installed
  shared-library gate configures its external CMake consumer.
- Added dependency `bin` directories to the Windows consumer runtime search
  path so installed DLL validation can load vcpkg-built dependencies.
- Stopped forwarding the single-config `CMAKE_BUILD_TYPE` setting to nested
  consumers generated by Visual Studio or other multi-config generators.

## 0.4.120 - 2026-08-30

Changes compared with `0.4.119`.

### Fixed

- Pinned the Windows shared-library CI job to the Windows Server 2022 runner so
  its installed Visual Studio version matches the explicitly selected Visual
  Studio 17 2022 generator.

## 0.4.119 - 2026-08-30

Changes compared with `0.4.118`.

### Added

- Added bounded transactional reverse translation from exact descriptive XMP
  properties into native IPTC-IIM title, caption, byline, keyword, copyright,
  credit, and source datasets.
- Added repeated creator/keyword reconciliation, explicit source and conflict
  policies, dirty tombstone propagation, IPTC byte limits, resource ceilings,
  and a thin Python `Document.translate_descriptive_metadata(...)` wrapper.

### Changed

- Non-ASCII descriptive translation now declares IPTC UTF-8 only when existing
  datasets can be preserved or replaced without reinterpreting unrelated
  legacy high-bit bytes; incompatible charset state fails transactionally.
- JPEG APP13 and TIFF tag 33723 preparation now rebuild dirty decoded IPTC
  datasets instead of replaying a stale preserved Photoshop IPTC resource.

### Tests And Validation

- Added singleton, repeated-value, conflict, ambiguity, operation-limit,
  byte-limit, UTF-8 declaration, legacy-encoding, and removal regressions.
- Added native JPEG and TIFF write/decode round trips for translated
  descriptive IPTC plus a stale raw-resource rebuild regression.

## 0.4.118 - 2026-08-30

Changes compared with `0.4.117`.

### Added

- Added bounded transactional reverse creation-date translation from edited
  `xmp:CreateDate`, `photoshop:DateCreated`, and XMP
  `exif:DateTimeOriginal` into native EXIF and IPTC date groups.
- Added explicit source-selection and native conflict policies, caller-lowered
  entry and operation limits, dirty tombstone propagation, and a thin Python
  `Document.translate_creation_dates(...)` wrapper.

### Changed

- Reverse date translation now rejects malformed dates and any projection that
  would discard time or subsecond precision. EXIF timezone/subsecond companion
  tags, lexical negative-zero timezones, and owned source provenance are
  preserved.
- Documented the explicit `create/edit -> translate -> prepare/write` workflow;
  transfer and writing do not synchronize edited XMP into native families
  implicitly.

### Tests And Validation

- Added conflict, ambiguity, operation-limit, malformed-input, precision,
  date-only, timezone, negative-zero, and removal regressions.
- Added native JPEG and TIFF write/decode round trips for translated EXIF and
  IPTC creation dates, plus Python thin-wrapper coverage.

## 0.4.117 - 2026-08-29

Changes compared with `0.4.116`.

### Added

- Added strict paired IPTC-IIM creation-date projection to portable XMP:
  `DateCreated` plus `TimeCreated` emits `photoshop:DateCreated`, while
  `DigitalCreationDate` plus `DigitalCreationTime` emits `xmp:CreateDate`.

### Changed

- Valid IPTC dates now remain usable as date-only XMP values when their
  optional time companion is missing or malformed. Invalid Gregorian dates
  and time-only values are not projected.
- Added `photoshop:DateCreated` to managed standard-property reconciliation so
  canonical writeback replaces stale existing XMP only when a valid generated
  value is available.

### Tests And Validation

- Added date-only, timezone, leap-date, malformed-input, conflict-policy, and
  canonicalization regressions.
- Added JPEG and TIFF write/decode round trips for both generated creation-date
  properties.

## 0.4.116 - 2026-08-29

Changes compared with `0.4.115`.

### Added

- Added bounded BMFF foreign-graph normalization for compact `iloc` offset and
  length fields used by HEIF, AVIF, and CR3 metadata insertion.
- Added conservative Canon MakerNote source-layout recognition with an explicit
  ambiguous source-offset-basis trust result and matching Python exposure.

### Changed

- Compact BMFF `iloc` graphs now grow offset and length fields to explicit
  32-bit widths when required. Omitted extent lengths fail safely because their
  source-to-end semantics cannot be preserved while the graph grows.
- Finalized EXR as a stable host-emission target for the current roadmap;
  existing-file rewrite and late-bound header patching remain host-owned.
- Reconciled the transfer and writing readiness estimates with completed writer
  contracts and compare-backed release gates.

### Tests And Validation

- Added HEIF, AVIF, and CR3 write/read-back tests for compact `iloc` graphs and
  malformed omitted-length rejection.
- Added Canon and Nikon MakerNote layout-trust regressions.

## 0.4.115 - 2026-08-28

Changes compared with `0.4.114`.

### Added

- Added stable Prepared Transfer Handoff Instance v1 with a move-only opaque
  per-worker owner cloned from an immutable prepared handoff.
- Added strict fixed-width time patch batches with POD result codes, complete
  prevalidation, failure atomicity, required slots, exact serialized widths,
  duplicate/invalid-field rejection, and aliased-input rejection.
- Added independent runtime contract-version checks, patch code names/messages,
  exact-width/slot-count field descriptors, indexed operation views, and
  callback replay for mutable worker instances.

### Changed

- Worker creation copies only one contiguous payload buffer plus block ranges,
  typed operations, semantic kinds, EXR subviews, and patch slots. It does not
  copy routes, policy strings, generated sidecars, or rebuild operation plans.
- Immutable handoffs can now serve as concurrently shareable templates for
  independently mutable realtime workers. An instance owns its state and does
  not borrow the source handoff.

### Tests And Validation

- Added opaque-layout, move ownership, source-lifetime independence,
  cross-worker isolation, all-target cloning, EXR view, strict-width,
  transactional failure, alias rejection, and callback replay tests.
- Added concurrent regression coverage with independent workers repeatedly
  patching and replaying their own prepared payloads.
- Extended the installed shared-library consumer with the mutable-instance
  runtime contract and default lifecycle checks.

## 0.4.114 - 2026-08-21

Changes compared with `0.4.113`.

### Added

- Added stable Prepared Transfer Handoff v1 with a move-only opaque owner for
  target-specific metadata payloads and typed codec operations.
- Added transactional snapshot preparation, allocation-free indexed operation
  views, allocation-free callback replay, semantic-family classification, and
  typed EXR name/type/value views without exposing route strings.
- Added runtime handoff contract-version checks and stable structured result
  code names/messages.

### Changed

- Repeated handoff replay now uses operations compiled once during preparation
  instead of rebuilding and validating an adapter vector for every replay.
- EXR typed operation resolution now validates and resolves one operation in
  constant time instead of rebuilding the complete adapter view.
- Kept raw-carrier passthrough, mutable time patches, prepared-bundle fields,
  prepared artifact persistence, and destination editing outside stable
  Handoff v1.

### Tests And Validation

- Added opaque-layout, move ownership, transactional failure, repeated replay,
  callback failure, EXR typed-view, and result-contract tests.
- Added typed handoff coverage for JPEG, TIFF, DNG, JXL, WebP, PNG, JP2, HEIF,
  AVIF, CR3, and EXR targets.
- Extended the installed shared-library consumer with handoff contract and
  lifecycle checks.

## 0.4.113 - 2026-08-21

Changes compared with `0.4.112`.

### Added

- Added Host Adoption Profile v1 with an allocation-free runtime descriptor for
  exact compile-time/linked-library contract checks.
- Added independent v1 contract versions for positional input, positional read
  results, decoded source snapshots, structured diagnostics, and typed FlatHost
  import.
- Added a host-profile regression covering positional read, diagnostics,
  snapshot persistence, FlatHost reconciliation, target preparation, and typed
  codec handoff.

### Changed

- Stabilized the narrow positional source/snapshot/diagnostic surface, decoded
  snapshot object and v1 persistence, and typed FlatHost reconciliation.
- Kept low-level format scanners/decoders, convenience snapshot readers,
  prepared bundles, adapter-view construction/execution, and raw-carrier
  passthrough policy explicitly experimental.
- Documented the stable high-throughput reconciliation sequence, ownership,
  concurrency, residual, and compatibility requirements.

### Tests And Validation

- Added runtime descriptor/ABI-shape checks and an end-to-end stable-state to
  typed-operation workflow regression.
- Extended the installed shared-library consumer to verify the linked Host
  Adoption Profile v1 contract.

## 0.4.112 - 2026-08-21

Changes compared with `0.4.111`.

### Added

- Added bounded positional RAF preview-JPEG and FujiIFD/TIFF metadata scanning
  with range-relative block descriptors and no preview entropy reads.
- Added bounded positional X3F `IMA2`/`IMAG` section-JPEG metadata scanning
  through the declared `SECd`/`SECi` structure.
- Added compact source-built RAF/X3F fixtures containing native metadata,
  embedded EXIF/XMP, and denied synthetic entropy ranges.

### Changed

- Positional RAF/X3F snapshot assembly now decodes declared embedded EXIF,
  XMP, MakerNote, FujiIFD, and native metadata without materializing preview or
  image payloads.
- Shared positional payload assembly now decodes RAF FujiIFD/TIFF directly from
  a bounded source subrange and disables recursive embedded-container decoding
  for inner preview EXIF.
- Undeclared source-wide fallback signature searches remain explicit residuals
  instead of forcing whole-file reads.

### Tests And Validation

- Added callback-versus-contiguous block parity, nonzero source-range offset,
  measure-count, embedded snapshot, entropy-denial, malformed-range, and
  cumulative request-limit coverage for RAF and X3F.

## 0.4.111 - 2026-08-21

Changes compared with `0.4.110`.

### Added

- Added bounded positional native metadata decoders for Fujifilm RAF, Sigma
  X3F, and Canon CRW/CIFF with caller-owned structural/value scratch and
  cumulative request/byte limits.
- Added allocation-free structured positional-read diagnostics for source I/O,
  malformed container/payload families, resource ceilings, scratch sizing,
  incomplete requested MakerNote lanes, raw-carrier truncation, and residual
  metadata paths.
- Added compact source-built public integration fixtures for TIFF/DNG with a
  representative Nikon MakerNote, WebP, AVIF, JP2, JXL, RAF, X3F, and CRW.

### Changed

- Positional source snapshot assembly now accepts explicitly typed RAF, X3F,
  and CRW sources without whole-file buffering or reads through image payload
  ranges.
- Optional RAF/X3F embedded-preview recursion, unsupported nested MakerNote
  paths, selected BMFF source-wide enrichment, and whole-file raw-carrier
  preservation remain explicit residuals rather than hidden whole-file reads.

### Tests And Validation

- Added contiguous-versus-callback native RAW parity, denied pixel-range,
  malformed offset, caller-scratch, and cumulative request-limit regressions.
- Added portable fixture decode and structured diagnostic count/detail/message
  coverage.

## 0.4.110 - 2026-08-21

Changes compared with `0.4.109`.

### Added

- Added transactional FlatHost remove-by-entry and remove-by-unique-name
  operations that preserve stable `Dirty | Deleted` tombstones, provenance,
  entry ids, and snapshot raw-carrier links.
- Added canonical validation for the codec-facing typed adapter operation list
  and a typed EXR name/type/value accessor that avoids route or payload-framing
  parsing in host codecs.
- Added a tested deferred reconciliation workflow for deserializing a snapshot,
  importing changed/added/removed host attributes, preserving untouched complex
  metadata, and preparing target payloads.

### Changed

- Split the stable v1 adapter operation schema from the broader experimental
  transfer contract through `kPreparedTransferAdapterContractVersion`.
- Compatibility-locked canonical source-snapshot v1 bytes while retaining
  bounded transactional parsing and atomic unknown-version rejection.
- Kept C++17 wrappers explicitly downstream-owned; OpenMeta remains a C++20
  library and does not plan an in-repository C++17 bridge.

### Tests And Validation

- Added FlatHost tombstone identity, ambiguity, non-empty-value rejection, and
  source-immutability coverage.
- Added exact adapter field mutation detection, typed EXR resolution, snapshot
  reconciliation, raw-carrier link retention, and canonical v1 byte-vector
  regressions.

## 0.4.109 - 2026-08-20

Changes compared with `0.4.108`.

### Added

- Added allocation-free positional GIF extension scanning and bounded EXR
  header traversal with caller-owned structural and attribute-value scratch.
- Added callback-backed logical metadata payload extraction for direct,
  GIF-sub-block, multipart JPEG, Deflate, and Brotli carriers.
- Added bounded positional source-snapshot assembly for JPEG, PNG, WebP, GIF,
  JP2, JXL, ISO-BMFF, TIFF/DNG, and EXR sources without whole-file buffering.

### Changed

- Positional snapshot results retain exact source-I/O diagnostics and aggregate
  request and byte ceilings across scan, payload, decode, and optional raw
  carrier reads.
- Positional snapshots own the finalized decoded store while keeping scanner,
  payload, decompression, and value workspaces caller-owned and operation-local.
- Unconverted native RAW, embedded-container, BMFF source-wide enrichment, and
  whole-file TIFF/EXR raw-carrier paths are reported as explicit residuals.

### Tests And Validation

- Added callback-versus-contiguous GIF/EXR parity, nonzero source-range,
  malformed/I/O/scratch, payload reassembly, image-data skip, cumulative
  budget, raw-carrier linkage, and snapshot assembly regressions.
- Extended positional container fuzz coverage to GIF extension scanning.
- Made file-backed transfer and LibRaw adapter tests use portable host temporary
  directories so the full suite can run on Windows as well as Unix hosts.

## 0.4.108 - 2026-08-20

Changes compared with `0.4.107`.

### Added

- Added allocation-free positional scan and measure APIs for PNG, WebP, JP2,
  JXL, and ISO-BMFF sources using caller-owned read windows.
- Added callback-backed BMFF brand, metadata-item/location/reference, ICC
  property, JP2 UUID, and nested CR3 metadata traversal.

### Changed

- PNG and WebP chunk scanning and JP2/JXL/BMFF box scanning now share parser
  logic between contiguous and callback sources, preserving descriptor order,
  normalization, and range-relative offsets.
- Positional scanners skip pixel codestream, RIFF image, and BMFF `mdat`
  payloads while retaining explicit source-I/O and resource-limit diagnostics.
- WebP scanning now rejects an odd-sized RIFF chunk whose required padding byte
  lies outside the declared RIFF extent.

### Tests And Validation

- Added contiguous-versus-callback descriptor and measure parity, non-zero
  source-range, scratch/I/O, incremental PNG prefix, BMFF item-table, and large
  payload-skip regressions for all five formats.
- Extended container fuzzing through every positional chunk/box scanner.

## 0.4.107 - 2026-08-20

Changes compared with `0.4.106`.

### Added

- Added allocation-free `scan_jpeg_random_access(...)` and
  `measure_scan_jpeg_random_access(...)` entry points over caller-owned
  positional sources and read windows.
- Added explicit combined scan and source-I/O results, preserving short-read,
  I/O, cancellation, source-change, scratch, and resource-limit diagnostics.

### Changed

- Callback JPEG scanning reads bounded marker and metadata-prefix windows, stops
  at Start of Scan, and does not fetch entropy-coded image data.
- Positional JPEG output preserves range-relative block offsets and the existing
  EXIF, XMP, ICC, MPF, APP4, APP11 JUMBF, Photoshop IRB, FLIR, and comment
  classification contract, including multipart APP11 normalization.

### Tests And Validation

- Added contiguous-versus-callback descriptor parity, non-zero source-range,
  bare-XMP probe, APP11 reassembly, entropy-skip, malformed-segment, memory
  source, scratch-shortage, short-read, I/O-error, and byte-budget regressions.
- Extended the container-scan fuzz target through callback-backed JPEG scanning
  and the same discovered-range invariants.

## 0.4.106 - 2026-08-20

Changes compared with `0.4.105`.

### Added

- Added callback-backed Nintendo CameraInfo, Casio QVC/DCI, FLIR, and Ricoh
  MakerNote decoding with checked absolute, MakerNote-relative, and
  vendor-specific offset bases.
- Added callback parity for Minolta main IFDs and their existing derived binary
  tables when the declared MakerNote payload is available in caller scratch.
- Added source-backed Ricoh ImageInfo, CameraInfo subdirectory, FaceInfo,
  SerialInfo, and Theta subdirectory expansion without materializing the outer
  TIFF stream.

### Changed

- Ricoh callback IFD scoring now uses the same local-offset plausibility rule as
  contiguous decoding before applying mixed-base value resolution. This avoids
  selecting structurally plausible binary data or opposite-endian tables.
- Source-backed Olympus traversal now validates nested IFD structure and value
  ranges before decoding, matching the contiguous decoder's silent rejection
  of invalid subdirectory pointers.
- Random-access residual reporting no longer classifies supported Nintendo,
  Casio, Minolta, FLIR, or Ricoh MakerNotes as unconverted.

### Tests And Validation

- Added callback regressions for Nintendo CameraInfo, Casio Type 2,
  Casio zero-count vendor entries, Minolta-derived binary tables, FLIR classic
  IFDs, and Ricoh classic `MakerNote + 8` value offsets.

## 0.4.105 - 2026-08-20

Changes compared with `0.4.104`.

### Added

- Added callback-backed Kodak MakerNote decoding for fixed-layout records and
  outer-TIFF-relative Type 8, Type 10, and Type 11 IFDs, including pointer and
  embedded vendor subtables.
- Added a bounded Kodak nearby-IFD resolver that reads through caller-owned
  random-access windows without materializing the TIFF payload.

### Changed

- Kodak callback candidate scoring now matches contiguous TIFF scoring by
  rejecting invalid TIFF types before considering zero-length values. This
  prevents binary bytes from outranking the intended vendor IFD.
- Random-access residual reporting no longer classifies supported Kodak
  MakerNotes as unconverted. Source I/O and value-scratch shortages remain
  explicit through `ExifRandomAccessDecodeResult`.

### Tests And Validation

- Added callback regressions for Kodak KDK records, outer-TIFF out-of-line
  values, nested pointer tables, invalid-type decoys, and value-scratch
  requirements.

## 0.4.104 - 2026-08-19

Changes compared with `0.4.103`.

### Added

- Added callback-backed Fujifilm MakerNote decoding for self-relative
  `FUJIFILM`/`GENERALE` IFDs and both bounded General Imaging Type 2 offset
  windows without copying or patching source bytes.

### Changed

- The source-backed classic IFD reader can accept a caller-supplied entry count
  for layouts that store the count outside the readable wire table. Existing
  callers retain ordinary on-wire count parsing.
- Random-access decoding now limits the explicit mixed-base residual list to
  Kodak, Ricoh, Nintendo, Casio, Minolta, and FLIR paths plus external Canon
  derived subtables.

### Tests And Validation

- Added callback regressions for Fujifilm out-of-line values and General
  Imaging Type 2 virtual-count decoding.

## 0.4.103 - 2026-08-19

Changes compared with `0.4.102`.

### Added

- Added callback-backed Olympus decoding for legacy outer-TIFF-relative values
  and nested sub-IFDs while retaining complete MakerNote-relative `OLYMPUS` and
  `OM SYSTEM` decoding through the existing bounded local path.
- Added callback-backed Panasonic IFD values and FaceDetInfo, FaceRecInfo, and
  TimeInfo binary tables, including MakerNotes with a truncated trailing
  next-IFD pointer.
- Added callback parity for self-contained Samsung STMN/SamsungIFD and Type2
  PictureWizard MakerNotes.

### Changed

- Panasonic callback candidate selection now prefers the documented IFD origin
  before the bounded fallback scan, preventing binary payload bytes from
  outranking a complete root IFD.
- Random-access decoding continues to report mixed-base Fuji, Kodak, Ricoh,
  Nintendo, Casio, Minolta, and FLIR paths as explicit residuals until each
  vendor resolver is converted and verified.
- Replaced overflow-prone Samsung value-end arithmetic with subtraction-based
  range validation.

### Tests And Validation

- Added callback regressions for Olympus outer-relative values, Olympus and OM
  System nested sub-IFDs, Panasonic binary tables and truncated IFD tails, and
  Samsung STMN and Type2 derived tables.

## 0.4.102 - 2026-08-19

Changes compared with `0.4.101`.

### Added

- Added a reusable allocation-free nested TIFF offset resolver that separates
  checked inline, absolute, MakerNote-relative, embedded-TIFF-relative, and
  signed adjusted-base translation from callback-backed byte acquisition.
- Added callback-backed Nikon embedded-TIFF recursion, including values beyond
  the declared MakerNote length, Nikon type 1 outer-TIFF IFD decoding, Sony
  outer-TIFF-relative IFD decoding, and Canon absolute/MakerNote/adjusted-base
  selection.

### Changed

- Canon MakerNotes whose required values are contained in the declared payload
  retain the complete existing main and derived BinaryData decoder behavior.
  When Canon values extend outside that payload, the callback path decodes the
  source-backed main IFD but reports the remaining derived-table work through
  `nested_payloads_skipped` instead of claiming parity.
- Nested callback reads now preserve aggregate request/byte ceilings and merge
  source failures, scratch requirements, decode status, and residual counts
  into the outer operation without global state or full-source materialization.
- Removed undefined signed-underflow arithmetic from negative adjusted
  MakerNote-base validation while preserving checked upper overflow and
  negative resolved-offset rejection.

### Tests And Validation

- Added callback regressions for Canon adjusted bases and external derived-table
  residuals, Sony outer-TIFF-relative values, Nikon type 1, Nikon embedded TIFF,
  Nikon values beyond the declared MakerNote byte count, and aggregate nested
  request/byte budgets.

## 0.4.101 - 2026-08-19

Changes compared with `0.4.100`.

### Added

- Added allocation-free source ranges and caller-owned read windows for bounded
  positional decoding. Callback reads reuse structural cache storage, honor the
  existing request/byte ceilings, and report undersized scratch explicitly.
- Added `decode_exif_tiff_random_access(...)` for classic TIFF, BigTIFF, DNG,
  Panasonic RW2, and Olympus ORF header/IFD traversal without materializing the
  complete source. Large values use caller-owned scratch after type, count,
  range, and metadata-size validation.
- Added callback-backed nested decoding for PrintIM, GeoTIFF, Pentax
  `DNGPrivateData`, and bounded self-contained MakerNotes. The combined result
  reports required value scratch and unsupported outer-TIFF-relative nested
  payloads instead of silently claiming complete parity.

### Changed

- Routed the existing span-based `decode_exif_tiff(...)` API through the same
  random-access entry point. Contiguous ranges retain direct zero-copy views and
  the complete existing nested-decoder behavior.
- Ordered callback IFD traversal and out-of-line value reads to preserve the
  structural read window, reducing avoidable rereads in realtime and
  range-backed pipelines.

### Tests And Validation

- Added source-range translation, read-ahead/cache-hit, classic/BigTIFF
  span-versus-callback parity, RW2/ORF, malformed offset, short-read,
  request/byte budget, structural/value scratch, PrintIM, GeoTIFF, Pentax DNG
  private data, and residual MakerNote reporting regressions.

## 0.4.100 - 2026-08-19

Changes compared with `0.4.99`.

### Added

- Added the allocation-free `RandomAccessSource` foundation for high-performance
  image-processing and transcoding hosts. It supports caller-owned contiguous
  memory or a synchronous positional callback without depending on a host I/O
  library.
- Added exact-read accounting and explicit short-read, I/O, cancellation,
  source-change, range, callback-contract, request-count, total-byte, and
  single-read failure results. Accounting and first-failure state remain local
  to each operation for concurrent immutable-source use.

### Changed

- Split the random-access roadmap into a completed consumer-neutral source
  contract and per-format scanner/decoder conversions. Existing TIFF/DNG and
  other decode APIs remain span/mapped-file based until those paths can use
  caller-owned scratch windows without full-source materialization.

### Tests And Validation

- Added memory/callback, exact-range, short-read, sticky-failure, independent
  accounting, resource-budget, source-change, cancellation, I/O-error, and
  callback-contract regressions for the new input primitive.

## 0.4.99 - 2026-08-19

Changes compared with `0.4.98`.

### Added

- Added versioned, bounded target-neutral serialization for
  `TransferSourceSnapshot`. Round trips preserve decoded store blocks, duplicate
  entry order, typed values, provenance, flags, optional raw carriers, and
  decoded-entry links without treating preserved carrier bytes as safe rewrite
  evidence.
- Added transactional typed `FlatHost` import. Existing values can be updated by
  exact source-entry identity or by a unique exported name, while new arbitrary
  metadata requires an explicit `MetaKeyView`; ambiguous duplicate names are
  rejected instead of guessed or collapsed.

### Changed

- Reordered the host-integration roadmap around real random-access decoding.
  The planned callback must refactor scanners, payload extraction, and decoders
  to bounded `read_at` access and may not silently buffer an entire multi-GB
  source.
- Documented `ExportNamePolicy::Spec` for hosts that require specification names
  such as `Exif:ISOSpeedRatings` and `Exif:ExposureBiasValue`.

### Fixed

- Corrected shared-library documentation to report ABI major 2.
- Corrected snapshot byte-read documentation to use the existing
  `ReadTransferSourceSnapshotOptions` type.

### Tests And Validation

- Added C++ round-trip, malformed-input, resource-limit, atomic-failure,
  duplicate-name, identity-update, explicit-key, and typed-value regressions for
  the new snapshot and FlatHost import contracts.

## 0.4.98 - 2026-08-17

Changes compared with `0.4.97`.

### Added

- Added `makernote_layout_transfer_audit_from_store(...)` and matching thin
  Python `Document` / `TransferSourceSnapshot` methods. The bounded audit
  distinguishes canonical Nikon type 1 outer-TIFF-relative notes from type 3
  notes containing an embedded TIFF at byte 10.
- Added conservative embedded-TIFF structure validation for canonical Nikon
  type 3 payloads. The result explicitly leaves vendor-private offsets,
  checksums, and semantic roundtrip validation unverified.

### Fixed

- The installed shared-library consumer gate now forwards the configured CMake
  dependency prefix to its nested configure, so optional public dependencies
  such as `dng_sdk` remain discoverable in custom SDK prefixes.
- Public CMake smoke and release gates now honor
  `OPENMETA_PYTHON_EXECUTABLE` instead of assuming a `python3` executable
  name, including native Windows test runs.

### Tests And Validation

- Added C++ regressions for Nikon type 1/type 3 layout classification,
  truncated type 3 rejection, and byte-identical type 3 MakerNote preservation
  with selected Nikon field read-back after JPEG and TIFF transfer.
- Extended the Python transfer-probe smoke test with the thin MakerNote layout
  audit surface and conservative capability flags.

## 0.4.97 - 2026-08-17

Changes compared with `0.4.96`.

### Added

- Added `makernote_transfer_audit_from_store(...)` and matching thin Python
  `Document` / `TransferSourceSnapshot` methods. The audit distinguishes raw
  opaque payloads from decoded-only fields and reports that generic offset
  relocation, vendor checksum repair, semantic validation, and raw-carrier
  passthrough are unavailable.
- Added deterministic MakerNote policy reasons for unverified opaque-byte
  preservation and unavailable rewrites that are dropped.

### Changed

- Explicit MakerNote `Rewrite` requests now fail closed by dropping the raw
  payload instead of silently preserving it and describing that preservation
  as a rewrite fallback.
- MakerNote `Keep` remains the compatible-file default, but policy diagnostics
  now state that it preserves opaque bytes without relocating nested offsets,
  repairing vendor checksums, or proving semantic readability after repacking.
- Fixed Minolta, Panasonic, and Sony binary-subdirectory decoding so
  derived-entry emission cannot invalidate source entry or arena views during
  the same decode pass.

### Tests And Validation

- Added C++ regressions for raw and decoded-only MakerNote audits, unverified
  `Keep` behavior, and fail-closed `Rewrite` behavior.
- Verified the MakerNote lifetime fixes with the previously failing
  cross-vendor and Sony full-suite test orders on both libc++ and MSVC.
- Extended the Python transfer-probe smoke test with the thin MakerNote audit
  surface and capability flags.

## 0.4.96 - 2026-08-14

Changes compared with `0.4.95`.

### Added

- Added bounded writer support for retained HEIF/AVIF/CR3 `iloc` records that
  use an explicitly self-contained version-0 `dinf`/`dref` `url ` or `urn `
  data entry. Non-self-contained and unresolved data references still fail
  closed.
- Added HEIF, AVIF, and CR3 regressions for self-contained data references,
  managed metadata relation/property remapping, strip cleanup, malformed item
  groups, and unsupported item-group versions.
- Made the metadata-transfer and EXR-adapter test sources self-contained and
  portable to MSVC, including the required large-object and UTF-8 source
  compile modes, so the same writer regressions can run on Windows.

### Changed

- Replacing a single unambiguous managed BMFF Exif, XMP, JUMBF, or C2PA item
  now remaps its item ID in retained `iref` relations, version-0 `grpl` item
  groups, and `ipma` associations. Strip operations and ambiguous replacements
  remove stale references instead of preserving dangling item IDs.
- Existing non-ICC `ipco` properties and their `ipma` associations are now
  preserved while managed metadata item IDs are remapped independently of ICC
  replacement.

## 0.4.95 - 2026-08-12

Changes compared with `0.4.94`.

### Added

- Added cumulative metadata-entry and arena-byte limits that remain shared
  across parent and nested decoders, including PrintIM and XMP paths.
- Added security regressions for malformed BMFF/BigTIFF sizes, C2PA
  certificate/key mismatches, cumulative decode amplification, unsafe output
  links, explicit verification failure, and terminal-control filenames.

### Changed

- Advanced the shared-library ABI major to `2` for the new public resource
  budget fields and cumulative store state.
- C2PA cryptographic signature success is now reported as
  `signature_verified_only`; `verified` is reserved for future verification
  that also binds the manifest to the complete asset. Certificate-bearing
  signatures are verified exclusively with the leaf certificate public key.
- Explicit C2PA verification now fails validation for every result except a
  future complete asset-bound `verified` result.
- BMFF and BigTIFF scanners now use overflow-safe ranges, checked box ends,
  strict forward progress, and bounded nested box traversal.
- Output and XMP sidecar persistence now use same-directory temporary files
  and atomic publication. No-overwrite mode rejects existing links and other
  destinations without a check-then-open race.
- Streaming transfer rejects direct, hard-link, and symlink aliases between
  its output and mapped inputs before writing.
- Command-line path diagnostics now escape terminal control bytes.

## 0.4.94 - 2026-08-10

Changes compared with `0.4.93`.

### Added

- Added an installed shared-library consumer gate. It stages the configured
  package, configures an independent CMake consumer against the installed
  `OpenMeta::openmeta_shared` target, links a `std::string` API call, and runs
  the result.
- Added Linux and Windows CI lanes for the shared-only install-consumer gate.
- Added the public shared-library ABI and runtime contract.

### Changed

- Shared Unix builds now hide implementation symbols and explicitly expose the
  declarations in OpenMeta public headers. Static implementation archives are
  also excluded from ELF dynamic exports; macOS shared builds reject static
  implementation dependencies that would leak through the dylib.
- Windows static archives and shared-library import archives now use distinct
  names, avoiding `openmeta.lib` collisions in dual-library builds. The DLL
  uses CMake's generated export table until the C++ API has a separately frozen
  per-symbol export surface.
- Shared libraries now carry ABI major `1` through platform versioning, while
  the CMake package publishes `OpenMeta_ABI_VERSION`.
- Shared packages no longer require compression, XML, crypto, or DNG SDK CMake
  dependencies merely to configure a consumer. Static packages continue to
  publish their full link closure.
- An installed package built with `OPENMETA_USE_LIBCXX=ON` now propagates its
  matching Clang/libc++ compile and link requirements to CMake consumers.
- MSVC builds now have an explicit `CMAKE_MSVC_RUNTIME_LIBRARY` contract which
  installed targets propagate to consumers, avoiding `/MT` and `/MD` mixing.
- Shared-library unit tests now retain private formatter coverage without
  exporting that implementation merely to satisfy a test-only call.
- Test targets now receive their required optional dependencies directly rather
  than relying on the private dependency closure of a shared OpenMeta target.

## 0.4.93 - 2026-08-03

Changes compared with `0.4.92`.

### Added

- Added a transfer regression that feeds logical metadata edits through every
  public target preparation and execution backend.
- Verified edited values reach JPEG, TIFF/DNG, JXL, WebP, PNG, JP2, HEIF,
  AVIF, CR3, and EXR routes while removed values do not reappear.

## 0.4.92 - 2026-08-02

Changes compared with `0.4.91`.

### Added

- Added the transactional v1 `edit_metadata(...)` C++ contract for bounded
  logical add, set, remove, and remove-all operations over finalized stores.
- Added deterministic repeated-field occurrence handling, explicit singleton
  conflicts and repair, request-order semantics, stable statuses, operation
  diagnostics, and resource limits shared with the Creation field map.
- Added provenance-preserving value updates, dirty tombstones, deterministic
  portable-XMP additions, and support for adding metadata to an empty finalized
  store without inventing source-block provenance.
- Added immutable thin Python editing operations and
  `Document.edit_metadata(...)`, returning a detached edited document.
- Added C++ creation/editing/transfer tests, a Python editing smoke gate, and
  public editing, quick-start, stability, and lifecycle documentation.

### Changed

- Creation and Editing now share one private logical-field descriptor and
  validation implementation, preventing mapping and constraint drift.
- `MetaStore` now exposes `is_finalized()` so transactional editing can reject
  incomplete source stores explicitly.
- Editing readiness is now tracked at about 75-80%; the active implementation
  sequence advances to Transfer, Translation, and Writing.

## 0.4.91 - 2026-07-31

Changes compared with `0.4.90`.

### Added

- Added the transactional v1 `create_metadata(...)` C++ contract for creating
  a finalized canonical portable-XMP `MetaStore` from bounded logical fields.
- Added common descriptive, rights, identity, capture, geometry, and exposure
  fields with typed text, integer, and rational constructors.
- Added deterministic creator/keyword collection construction, duplicate
  singleton rejection, UTF-8/XML validation, numeric constraints, request
  limits, stable statuses, and failed-field diagnostics.
- Added thin Python creation fields and `create_metadata(...)`, returning a
  normal `Document` that supports query, XMP output, and transfer snapshots.
- Added C++ creation/serialization/query tests, a Python creation smoke gate,
  and public field-mapping and safety documentation.
- Installed package configuration now resolves exported EXPAT and optional
  OpenSSL link dependencies before loading `OpenMetaTargets.cmake`.

### Changed

- Creation readiness is now tracked at about 70-75%; the active project
  sequence advances to Editing, Transfer, Translation, and Writing.

## 0.4.90 - 2026-07-30

Changes compared with `0.4.89`.

### Added

- Expanded the curated Fuzzy Search alias vocabulary and quality gate across
  descriptive, capture, geometry, color, RAW-processing, rights, location,
  lens, and history terminology.
- Added metadata-like adversarial negatives plus explicit ASCII separator,
  camel-case, acronym-boundary, and UTF-8 policy tests.
- Added the opt-in `OPENMETA_BUILD_BENCHMARKS` configuration and
  `openmeta_benchmark_fuzzy_search` deterministic scaling workload, compiled
  and executed by the RapidFuzz Release/libc++ CI job.
- Added a dedicated Fuzzy Search contract covering ranking, resource and
  threading bounds, quality gates, benchmark use, and indexing policy.

### Changed

- Fuzzy alias typos now require whole-phrase and per-token similarity, avoiding
  unrelated matches caused by one shared word.
- ASCII normalization now splits acronym-to-word boundaries such as
  `GPSLatitude` and `ICCProfile`.
- Fuzzy Search readiness is now tracked at about 80-85%; the active project
  sequence moves to Creation, Editing, Transfer, Translation, and Writing.
  Fuzzy Search resumes before Adapters and Utilities for Unicode/multilingual
  behavior and an optional immutable large-store index.

## 0.4.89 - 2026-07-30

Changes compared with `0.4.88`.

### Added

- Added optional bounded `fuzzy_search_metadata(...)` entry search with
  deterministic top-k ranking, exact/alias/fuzzy provenance, score cutoffs,
  stable tie-breaking, bounded returned names with truncation flags, and
  explicit ASCII-only query status.
- Added thin `Document.fuzzy_search(...)` and
  `TransferSourceSnapshot.fuzzy_search(...)` Python wrappers.
- Added a synthetic typo/alias quality corpus with precision, recall,
  false-positive, resource-bound, and ranking tests.
- Added a dedicated RapidFuzz-enabled Release/libc++ CI release gate.

### Changed

- Semantic Query no longer applies fuzzy partial matching while classifying XMP
  properties; tolerant free-text matching now stays in the separate ranked
  Fuzzy Search stage, preventing near names from changing metadata meaning.
- Fuzzy Search readiness is now tracked at about 70-80% after adding the
  standalone API and quality gates.

## 0.4.88 - 2026-07-29

Changes compared with `0.4.87`.

### Changed

- EXIF preparation now omits source `GlobalParametersIFD` and DNG
  `ExtraCameraProfiles` offsets when their child directories cannot be
  materialized, preventing stale source offsets from reaching destination
  metadata.
- TIFF/DNG rewrite preserves existing target-owned auxiliary IFD pointers and
  payload bytes when the corresponding source pointers are omitted.
- Project readiness documentation now tracks optional Fuzzy Search separately
  from measured exact and semantic Query coverage.

## 0.4.87 - 2026-07-28

Changes compared with `0.4.86`.

### Added

- Added explicit synthetic read-release gates for AVIF, GIF, JXL, and the
  shared TIFF carrier used by Sony SR2/SRF metadata.
- Added high-level prepared BMFF item/property payload emission through
  `ExecutePreparedTransferOptions::emit_output_writer`, including deterministic
  capacity preflight and emitted item/property summaries.

### Changed

- Read coverage documentation now distinguishes shared Sony TIFF-carrier
  coverage from native legacy SR2/SRF private-structure coverage.

## 0.4.86 - 2026-07-28

Changes compared with `0.4.85`.

### Added

- Added stable URI-preserving identity for unknown nested XMP namespaces,
  preventing equal local names from different namespaces from colliding.
- Added standard EXIF/TIFF value names for YCbCr positioning, sensitivity
  type, focal-plane units, sensing method, file source, scene type,
  rendering controls, subject-distance range, additional compression modes,
  photometric interpretations, and valid flash states.
- Added source-software interpretation for XMP `CreatorTool`, exact Camera Raw
  Settings source-processing classification, isolated EXIF GPS time
  candidates, and validated ASCII-byte EXIF date/GPS field handling.

### Changed

- Nested XMP namespace path construction now preflights URI expansion and path
  arithmetic before allocation.
- Concept text strips trailing NUL padding without changing stored metadata
  bytes, and Python text conversion preserves non-UTF-8 bytes through
  `surrogateescape`.
- Camera Raw Settings matching now requires the exact standard namespace
  instead of accepting lookalike vendor namespace strings.

## 0.4.85 - 2026-07-28

Changes compared with `0.4.84`.

### Added

- Added bounded interpretation for standard XMP Media Management
  resource-reference, manifest-reference, history-event, and version structures
  nested inside pantry items.
- Added pantry-qualified record scopes and exact document-lineage or
  document-history query semantics for those structures.

### Changed

- Unknown nested XMP namespaces now use a stable `ns:` path marker instead of
  collapsing to an unqualified property name.
- Pantry identity and format promotion now requires the defined XMP Media
  Management or Dublin Core namespace form, while unknown and vendor-specific
  pantry payloads remain raw metadata.

## 0.4.84 - 2026-07-24

Changes compared with `0.4.83`.

### Added

- Added source-bound legacy IPTC technical-image, audio-asset, and
  preview-asset records with exact `technical_image`, `audio`, and `preview`
  query semantics.
- Added normalized image component/layout, audio
  channel/content/rate/resolution/duration/outcue, preview format/version/data,
  and rasterized-caption roles.
- Added matching append-only C++ and thin Python enums.

### Changed

- IPTC concept resolution now consumes the raw byte representation produced by
  the IPTC-IIM decoder for known textual and date/time datasets.
- IPTC image layout remains distinct from EXIF rotation orientation, preview
  format/version values use big-endian decoding, and binary preview identity
  uses bounded size/hash keys without copying payloads into concept records.
- Legacy IPTC technical records are source-bound and unsafe for automatic
  rendered-image transfer.

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
