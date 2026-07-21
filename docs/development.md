# Development

See also: `docs/metadata_support.md` for current container/block/decode support
and `docs/interpretation_status.md` for the semantic interpretation matrix.
If you are looking for the shortest practical entry path, start with
`docs/quick_start.md` before this file.

If you already have a host encoder, SDK, or container API, use
`docs/host_integration.md` after the quick start.

## OpenMeta Structure

OpenMeta's public architecture is organized around a small set of user-facing
capabilities. Internally some of these split into more stages, but the public
model should stay compact:

| Area | Purpose | Readiness |
| --- | --- | --- |
| Decoding | Find metadata carriers and decode EXIF, XMP, IPTC, ICC, Photoshop IRB, JUMBF/C2PA, EXR, and related blocks into `MetaStore` entries. | High, about 98-100% for the current target scope. |
| Interpretation | Normalize names and values, group entries by meaning, and classify source-bound data such as RAW crop, exposure adjustment, color/profile/source-color-transform evidence, RAW curves/linearity metadata with descriptor-backed compressed-storage applicability, lens-correction, sensor, BMFF brand/item-property associations and rollups including `avio` AVIF brands, item groups, item semantic counts, whole-scene policy hints, graph-component summaries with per-component roles, member item IDs, semantic composition, typed relation counts, bounded `grid`/`iovl`/`iden` construction semantics with recursive item-offset descriptors and graph-cycle/source validation, bounded complete `tili` configuration/reference/offset-table interpretation, `container_graph` concepts, primary item properties, primary metadata-carrier flags, primary sidecar and scene summaries, and display-transform summaries, JUMBF labels, Photoshop IRB embedded carriers plus fixed-layout, XML/text, working-path and numbered clipping-path records, byte-count, descriptor-header, scalar/class/alias/enum/reference, and bounded nested list/object summaries, EXIF 3.1 correction/noise labels, version/firmware-style value formatting for selected EXIF/Nikon/Olympus/native RAF contexts, selected Sony ILCE-7RM6 correction-offset routing, Fujifilm flash white-balance naming, Apple/FLIR/JVC/GE/Reconyx/Microsoft/Nintendo/Sanyo scalar MakerNote labels, current Canon RF/Nikon Z lens labels, an ambiguous Pentax Sigma/Samsung/Tokina lens-family label, EXIF/XMP GPS timestamp composites, EXIF `OffsetTime*`/`SubSecTime*` date composition with normalized-instant conflict checks, separate camera/destination/shown/created GPS roles with scope-aware structured-location conflicts, language-aware descriptive roles, legacy editorial duplicate reconciliation, accessibility/taxonomy/registry/image-region/document-identity/lineage/history semantics, structured creator-contact/event/person/organization/product/artwork/rights/license/release/end-user/image-creator/image-supplier/image-asset/controlled-vocabulary/registry/image-region/image-region-boundary/resource-reference/resource-event/manifest-item/version/pantry records, explicit image-region shape and coordinate-unit contracts, independent privacy/policy sensitivity, computational, thermal, stitch/panorama capture state, and vendor-private fields. | High, about 98-99%. |
| Query | Find entries by name, fuzzy term, or semantic group, then expose normalized query candidates, structured interpretation records, bounded cross-family concept resolutions, transfer hints, sensitivity, and conflict flags for crop/border/active-area, exposure/gain, color/WB/profile/source-color-transform, orientation, date/time, GPS, descriptive fields including contact/event/person/organization/product/artwork/rights/license/release, editorial, accessibility, taxonomy, registry, image-region, document-identity, document-lineage, and document-history semantics, lens-correction, computational/thermal/stitch, RAW/source-processing fields, and BMFF derived-image construction and tiled-image configuration evidence across standard and vendor metadata. | High, about 98-99%. |
| Creation | Build fresh metadata entries from host-provided values. | Medium, about 55-65%. |
| Editing | Modify existing logical metadata entries while preserving valid surrounding structure. | Medium, about 60-70%. |
| Transfer | Move metadata between files using explicit compatible-file or rendered-image safety policies. | Medium-high, about 80-85%. |
| Translation | Project metadata between families, mainly bounded EXIF/IPTC/XMP portable mappings. | Medium, about 60-70%. |
| Writing | Serialize metadata and write or rewrite it into target containers. | Medium, about 65-75%. |
| Adapters | Thin integration layers for host APIs or format-specific ecosystems such as EXR, DNG SDK, LibRaw orientation mapping, and flat host exports. | Medium, about 60-70%. |
| Utilities | Small standalone helpers such as capability queries, compatibility dumps, safety audits, tag-name lookup, version-value formatting, and orientation conversion. | Medium, about 65-75%. |

Query results should expose both inspection-level matches and
interpreted candidates. A crop query, for example, may match separate
`DefaultCropOrigin` and `DefaultCropSize` tags, an `ActiveArea` rectangle,
vendor margin fields, or a raw integer array. OpenMeta should return the
source entries, confidence, value shape, match provenance, and any normalized
interpretation rather than hiding ambiguity behind a single value.

The first experimental C++ query surface is `openmeta/metadata_query.h`.
It returns both raw matches and normalized candidates for crop/active-area,
exposure/gain, white balance, color/profile, lens correction, orientation,
descriptive, and RAW-processing queries.
Crop queries include DNG crop tags, `ActiveArea`, Phase One/Leaf raw geometry,
Fujifilm RAF raw crop/zoom rectangles, Canon aspect/crop metadata, Nikon
Capture crop bounds, Sony panorama crop margins, and fuzzy crop/border-style
XMP property paths. The non-crop queries expose per-entry value candidates and
reuse standard tag names, selected DNG tags, fuzzy XMP paths, canonical
border-margin parsing, and vendor RAW-processing classification where
applicable.
They also append grouped candidates for related DNG color matrix/calibration/
reduction/forward matrix tags, DNG white-balance vector tags, and
lens-correction table groups. Color queries expose a distinct `color_profile`
semantic for EXIF color-space evidence, ICC header/tag entries, XMP
ICC/profile/color-space fields, and PNG profile text carriers. Vendor-classified
MakerNote/RAW fields can also form per-family grouped candidates for white
balance, color, raw-storage, sensor, computational, thermal, stitch/panorama,
source-processing, raw value curve, linearity-limit, calibration-curve, and
curve-control-point records.
RAW-processing queries add conservative groups for black/white levels,
linearization tables, RAW value curves, RAW linearity limits, RAW calibration
curves, RAW curve control points, CFA/sensor layout, source geometry,
raw-storage identifiers, and source-private processing buckets.
Exposure/gain concept resolution promotes exposure time, aperture, ISO,
exposure bias, exposure program/mode, gain, and raw exposure-adjustment records
into host-visible roles, with raw exposure adjustments kept unsafe for rendered
targets. Standard EXIF exposure program/mode and gain-control values and
selected Canon/Nikon/Sony/Fujifilm/Pentax/Olympus/Panasonic/Phase One/Kodak/
Minolta/Sigma/Samsung/Ricoh/Apple/FLIR/JVC/GE/Reconyx/Microsoft/Nintendo/
Sanyo MakerNote scalar print conversions are exposed as bounded labels when a
stable enum mapping is
available.
Version/firmware-like fields use `exif_tag_numeric_value_format(...)` and
`exif_tag_byte_value_format(...)` instead of enum-label lookup where the value
is a formatted payload rather than a closed numeric choice.
Current source-private aliases include camera-to-XYZ/RGB matrices, creative and
picture styles, film simulation, dynamic-range processing, optical/lens
correction, white-balance gains, and raw-development terms.
Grouped candidates use `matrix_set`, `vector_set`, and `table` value shapes.
Color matrix sets, white-balance vector sets, and lens-correction tables are
promoted only when the numeric payloads meet conservative minimum shapes; other
records stay visible as per-entry matches/candidates.
When
`OPENMETA_ENABLE_RAPIDFUZZ=ON`, the same query helpers also use RapidFuzz to
score near-miss XMP/property paths; default builds keep the deterministic
substring/tag matcher only. Each raw match reports `exact_match`,
`fuzzy_match`, and `fuzzy_score` so UI code can distinguish exact tag/name
matches from near-miss search hits.
Python `Document` and `TransferSourceSnapshot` mirror this as thin wrappers
returning the same match/candidate dictionary shape.

For code that wants an iterable semantic record stream instead of raw query
matches, use `openmeta/metadata_interpretation.h`. It projects query candidates
into records with query class, semantic kind, normalized shape, confidence,
source entries, and normalized geometry/value arrays where available.

For cross-family duplicated concepts, use `openmeta/metadata_concepts.h`.
It currently resolves orientation, date/time, exposure/gain, color/profile,
GPS, descriptive fields, geometry, lens-correction, and RAW-processing into
candidate lists with
candidate source entries, source families, preferred entries, normalized
compare keys, parsed date/time fields, date/time precision, timezone kind, GPS
altitude-reference state, distinct destination/shown/created coordinate roles,
structured-location scope, normalized descriptive language, additive
collection preference, structured record kinds and scopes for editorial,
rights, license, release, end-user, image-creator, image-supplier, and
image-asset, controlled-vocabulary, registry, image-region,
resource-reference, resource-event, and pantry records, independent policy
sensitivity,
canonical geometry origin/size/rect/margins,
normalized exposure values, full normalized value vectors for grouped
matrix/vector/table records, transfer hints, compatible and rendered safety
booleans, and same-role conflict flags. This is deliberately an
inspection/policy surface; host code still decides whether a conflict should be
shown, ignored, or corrected during editing/transfer.

## Build Prerequisites

- CMake `>= 3.20`
- A C++20 compiler (Clang is recommended; fuzzing requires Clang)
- Optional: Ninja (`-G Ninja`)

OpenMeta discovers optional dependencies via `find_package(...)`. If you install
deps into a custom prefix, pass it via `CMAKE_PREFIX_PATH` (example:
`-DCMAKE_PREFIX_PATH=/mnt/f/UBSd`).

## Optional Dependencies (Why They Exist)

OpenMeta's core scanning and EXIF/TIFF decoding do not require third-party
libraries. Some metadata payloads are compressed or structured; these optional
dependencies let OpenMeta decode more content:

- **Expat** (`OPENMETA_WITH_EXPAT`): parses XMP RDF/XML packets (embedded blocks
  and `.xmp` sidecars). Expat is used as a streaming parser so OpenMeta can
  enforce strict limits and avoid building a full XML DOM from untrusted input.
- **RapidFuzz** (`OPENMETA_ENABLE_RAPIDFUZZ`): opt-in semantic-query name
  matching for inspection/search UI. It is disabled by default; when enabled,
  CMake requires either a `rapidfuzz::rapidfuzz` package target or
  `OPENMETA_RAPIDFUZZ_INCLUDE_DIR` pointing at headers containing
  `rapidfuzz/fuzz.hpp`.
- **zlib** (`OPENMETA_WITH_ZLIB`): inflates Deflate-compressed payloads,
  including PNG `iCCP` (ICC profiles) and compressed text/XMP chunks (`iTXt`,
  `zTXt`).
- **Brotli** (`OPENMETA_WITH_BROTLI`): decompresses JPEG XL `brob` "compressed
  metadata" boxes so wrapped metadata payloads can be decoded.
- **Draft C2PA verify scaffold** (`OPENMETA_ENABLE_C2PA_VERIFY`,
  `OPENMETA_C2PA_VERIFY_BACKEND`): enables backend selection/reporting fields
  (`none|auto|native|openssl`) and draft verification flow. Native backend
  availability is platform-based (Windows/macOS), while OpenSSL availability
  is discovered via `find_package(OpenSSL)` when needed. By default this
  reports cryptographic signature status and trust-chain detail separately;
  use `--c2pa-verify-require-trusted-chain` when validation must fail for an
  untrusted or missing certificate chain.

If you link against dependencies that were built with `libc++` (common when
using Clang), configure OpenMeta with:

```bash
-DOPENMETA_USE_LIBCXX=ON
```

When CTest launches external validation tools from the same dependency prefix,
those tools must also be able to find their matching C++ runtime libraries. If
the runtime is not discoverable through the system loader, pass an explicit
runtime directory:

```bash
-DOPENMETA_TEST_RUNTIME_LIBRARY_PATH=/path/to/runtime-libs
```

## Versioning

`VERSION` is the single source of truth for the project version:
- CMake reads `VERSION` and sets `PROJECT_VERSION`.
- The Python wheel version is derived from `VERSION` (via scikit-build-core metadata).

## CLI Tools

`metavalidate` checks decode-status health and DNG/CCM validation:

```bash
#Basic validation
./build/metavalidate input.dng

#Strict mode : warnings fail the file
./build/metavalidate --strict input.dng

#Machine - readable JSON output
./build/metavalidate --json input.dng

#Validate with sidecar + MakerNotes + C2PA verify status
./build/metavalidate --xmp-sidecar --makernotes --c2pa-verify input.jpg

#Require C2PA signature verification and a trusted certificate chain
./build/metavalidate --c2pa-verify --c2pa-verify-require-trusted-chain input.jpg
```
`metavalidate` CLI is a thin wrapper over `openmeta::validate_file(...)`.
Machine-readable JSON output includes issue codes suitable for gating, for
example `xmp/output_truncated` and `xmp/invalid_or_malformed_xml_text`.

`metadump` is the general dump/save tool:

```bash
#Lossless sidecar
./build/metadump --format lossless input.jpg output.xmp

#Portable sidecar
./build/metadump --format portable --portable-include-existing-xmp input.jpg output.xmp

#Portable sidecar with ExifTool GPS time alias compatibility
./build/metadump --format portable --portable-exiftool-gpsdatetime-alias input.jpg output.xmp

#Portable sidecar + draft C2PA verify scaffold status reporting
./build/metadump --format portable --c2pa-verify --c2pa-verify-backend auto input.jpg output.xmp

#Portable sidecar with trusted-chain C2PA verification required
./build/metadump --format portable --c2pa-verify --c2pa-verify-require-trusted-chain input.jpg output.xmp

#Explicit input / output form
./build/metadump -i input.jpg -o output.xmp

#Extract first embedded preview
./build/metadump --extract-preview --first-only input.jpg preview.jpg

#If multiple previews exist, --out gets auto - suffixed:
#preview_1.jpg, preview_2.jpg, ...
./build/metadump --extract-preview input.arq preview.jpg
```

Portable sidecar note:
- `exif:GPSTimeStamp` is emitted as XMP date-time text (`YYYY-MM-DDThh:mm:ssZ`)
  only when `GPSDateStamp` is available; otherwise it is skipped.
- Compatibility mode `--portable-exiftool-gpsdatetime-alias` emits
  `exif:GPSDateTime` instead of `exif:GPSTimeStamp`.
- Portable IPTC-IIM mapping covers `dc:*` plus selected `photoshop:*` and
  `Iptc4xmpCore:*` fields (for example city/state/country/headline/credit and
  location/country-code).

`metatransfer` is a transfer smoke tool for JPEG/TIFF packaging:

```bash
#read -> prepare -> emit simulation
./build/metatransfer input.jpg

#Portable vs lossless transfer-prepared XMP block
./build/metatransfer --format portable input.jpg
./build/metatransfer --format lossless input.jpg

#Write prepared payload bytes for inspection
./build/metatransfer --unsafe-write-payloads --out-dir payloads input.jpg

#Prepare once, emit many times (same bundle)
./build/metatransfer --emit-repeat 100 input.jpg

#Patch prepared EXIF time fields before emit
./build/metatransfer --time-patch DateTimeOriginal="2026:03:06 12:34:56" input.jpg

#Select explicit transfer policy for raw-sensitive families
./build/metatransfer --makernote-policy keep --jumbf-policy drop --c2pa-policy drop input.jpg

#Emit a draft unsigned C2PA invalidation payload for JPEG output
./build/metatransfer --no-exif --no-xmp --no-icc --no-iptc \
  --c2pa-policy invalidate input_with_c2pa.jpg

#Append one logical raw JUMBF payload into a prepared JPEG bundle
./build/metatransfer --no-exif --no-xmp --no-icc --no-iptc \
  --jpeg-jumbf payload.jumbf input.jpg

#Stage externally signed logical C2PA into a JPEG rewrite flow
./build/metatransfer --no-exif --no-xmp --no-icc --no-iptc \
  --jpeg-c2pa-signed signed_c2pa.jumb \
  --c2pa-manifest-output manifest.bin \
  --c2pa-certificate-chain chain.bin \
  --c2pa-key-ref signer-key \
  --c2pa-signing-time 2026-03-09T00:00:00Z \
  -o output.jpg input_with_c2pa.jpg

#Persist the semantic transfer payload batch for cross-process handoff
./build/metatransfer --dump-transfer-payload-batch payloads.omtpld input.jpg

#Load and inspect one persisted semantic transfer payload batch
./build/metatransfer --load-transfer-payload-batch payloads.omtpld

#Persist one final transfer package batch
./build/metatransfer --dump-transfer-package-batch package.omtpkg input.jpg

#Load and inspect one persisted final transfer package batch
./build/metatransfer --load-transfer-package-batch package.omtpkg

#Plan edit strategy without writing output
./build/metatransfer --mode auto --dry-run input.jpg

#Write edited JPEG output (metadata rewrite mode)
./build/metatransfer --mode metadata_rewrite -o output.jpg input.jpg

#Use separate metadata source and JPEG target stream
./build/metatransfer \
  --source-meta source.tif \
  --target-jpeg target.jpg \
  --mode metadata_rewrite \
  -o injected.jpg

#Use separate metadata source and TIFF target stream
./build/metatransfer \
  --source-meta source.jpg \
  --target-tiff target.tif \
  -o injected.tif

#Inject target-owned image facts when source and output pixels differ
./build/metatransfer \
  --target-jpeg target.jpg \
  --target-width 320 --target-height 240 \
  --target-samples-per-pixel 3 --target-bits-per-sample 8 \
  --target-photometric 2 --target-exif-color-space 1 \
  -o injected.jpg source.jpg
```

`metatransfer` is a thin CLI wrapper over the public transfer APIs. It uses
`prepare_metadata_for_target_file(...)` for source read/decode plus
`execute_prepared_transfer(...)` for time patching, route compile/emit, and
optional JPEG/TIFF edit plan/apply. When `--jpeg-jumbf` is used, the CLI also
calls `append_prepared_bundle_jpeg_jumbf(...)` before execute. The core also
exposes
`compile_prepared_transfer_execution(...)` plus
`execute_prepared_transfer_compiled(...)` for
`prepare once -> compile once -> patch/emit many` workflows. When `-o` is
used, the CLI passes a `TransferByteWriter` sink into the shared execution
path so edited output can stream directly to disk instead of always
materializing a full output buffer.
The `--target-width`, `--target-height`, `--target-orientation`,
`--target-samples-per-pixel`, `--target-bits-per-sample`,
`--target-sample-format`, `--target-photometric`,
`--target-planar-configuration`, `--target-compression`, and
`--target-exif-color-space` flags populate
`PrepareTransferRequest::target_image_spec`.
Current v1 behavior is:

- JPEG edit output is streamed directly from the shared core path.
- JPEG metadata-only emit can also stream marker bytes directly through the
  shared core API.
  - TIFF edit output uses the same sink API and only buffers the appended
  metadata tail; it no longer materializes a second full-file output buffer.
  - JPEG XL prepare/emit now shares the same transfer contract for backend
    emitter use:
    - `prepare_metadata_for_target(..., TransferTargetFormat::Jxl, ...)`
      currently builds `Exif`, `xml `, and bounded `jumb` box payloads plus
      the encoder ICC profile from `MetaStore`
    - `compile_prepared_bundle_jxl(...)` and
      `emit_prepared_bundle_jxl_compiled(...)` provide the same
      `prepare once -> compile once -> emit many` shape as JPEG/TIFF
    - `execute_prepared_transfer(...)` and
      `emit_prepared_transfer_compiled(..., JxlTransferEmitter&)` now accept
      JXL bundles
    - `jxl:icc-profile` is emitted through `JxlTransferEmitter::set_icc_profile(...)`
      and stays separate from the JXL box path
    - file-based prepare can preserve source generic JUMBF payloads and raw
      OpenMeta draft C2PA invalidation payloads as JXL boxes
    - store-only prepare can project decoded non-C2PA `JumbfCborKey` roots
      into generic JXL `jumb` boxes when no raw source payload is available
    - IPTC requested for JXL is projected into the existing `xml ` XMP box;
      OpenMeta does not create a raw IPTC-IIM JXL carrier
    - `build_prepared_jxl_encoder_handoff_view(...)` is the explicit
      encoder-side ICC handoff contract for JXL, and
      `build_prepared_jxl_encoder_handoff(...)` /
      `serialize_prepared_jxl_encoder_handoff(...)` add an owned persisted
      handoff object for cross-process reuse: at most one prepared
      `jxl:icc-profile` payload plus the remaining JXL box counts
    - `inspect_prepared_transfer_artifact(...)` is now the shared inspect
      entry point across persisted transfer artifacts:
      payload batches, package batches, persisted C2PA handoff/signed
      packages, and persisted JXL encoder handoffs
    - the JXL compile/emit path now rejects multiple prepared ICC profiles so
      the encoder handoff and backend execution contracts match
    - `build_prepared_transfer_emit_package(...)` plus
      `write_prepared_transfer_package(...)` can serialize direct JXL box
      bytes from prepared bundles, and
      `build_prepared_transfer_package_batch(...)` can materialize those bytes
      into one owned replay batch
    - bounded JXL file edit now uses the same package layer:
      it preserves the signature and non-managed top-level boxes, replaces
      only the metadata families present in the prepared bundle, and appends
      the prepared JXL boxes to an existing JXL container file
    - unrelated source JXL metadata boxes are preserved, and uncompressed
      source `jumb` boxes are distinguished as generic JUMBF vs C2PA for that
      replacement decision
    - when Brotli support is available, the same distinction is applied to
      compressed `brob(realtype=jumb)` source boxes before deciding whether
      to preserve or replace them
    - the package writer remains box-only, so it still rejects
      `jxl:icc-profile`; ICC remains encoder-only on JXL
    - CLI/Python `metatransfer` wrappers now expose this bounded edit path
      through `--target-jxl ... --source-meta ... --output ...` when the
      prepared bundle does not require `jxl:icc-profile`
    - JXL transfer now supports generated draft C2PA invalidation for
      content-bound source payloads, plus the bounded external-signer path:
      sign-request derivation, binding-byte materialization, signed-payload
      validation, staged `jxl:box-jumb` apply, and bounded file-helper edit
      execution
  - WebP prepare/emit now uses the same bounded transfer contract:
    - `prepare_metadata_for_target(..., TransferTargetFormat::Webp, ...)`
      currently builds `EXIF`, `XMP `, `ICCP`, and bounded `C2PA` RIFF
      metadata chunks from `MetaStore`
    - WebP `EXIF` chunk payloads contain direct TIFF bytes and intentionally
      omit the JPEG APP1 `Exif\0\0` preamble
    - `compile_prepared_bundle_webp(...)` and
      `emit_prepared_bundle_webp_compiled(...)` provide the same
      `prepare once -> compile once -> emit many` shape as JPEG/TIFF/JXL
    - `execute_prepared_transfer(...)` and
      `emit_prepared_transfer_compiled(..., WebpTransferEmitter&)` now accept
      WebP bundles
    - IPTC requested for WebP is projected into the existing `XMP ` chunk;
      OpenMeta does not create a raw IPTC-IIM WebP carrier
    - draft OpenMeta invalidation payloads and generated invalidation output
      use the `C2PA` RIFF chunk path
    - `build_prepared_transfer_emit_package(...)` plus
      `write_prepared_transfer_package(...)` can serialize direct WebP chunk
      bytes from prepared bundles, and
      `build_prepared_transfer_package_batch(...)` can materialize those bytes
      into one owned replay batch
    - full WebP signed C2PA rewrite remains follow-up work
  - ISO-BMFF metadata-item transfer now uses the same bounded contract for
    `HEIF` / `AVIF` / `CR3` targets:
    - `prepare_metadata_for_target(..., TransferTargetFormat::{Heif,Avif,Cr3}, ...)`
      currently builds `bmff:item-exif`, `bmff:item-xmp`, bounded
      `bmff:item-jumb`, bounded `bmff:item-c2pa`, and
      `bmff:property-colr-icc` payloads
    - EXIF is prepared as a BMFF item payload with the 4-byte big-endian
      TIFF-offset prefix plus full `Exif\0\0` bytes
    - IPTC requested for BMFF is projected into `bmff:item-xmp`; OpenMeta
      does not create a raw IPTC-IIM BMFF carrier
    - ICC requested for BMFF uses the bounded property path:
      `bmff:property-colr-icc` carries `u32be('prof') + <icc-profile>` as the
      payload bytes for a `colr` property, not a BMFF metadata item
    - file-based prepare can preserve source generic JUMBF payloads and raw
      OpenMeta draft C2PA invalidation payloads as BMFF metadata items
    - store-only prepare can project decoded non-C2PA `JumbfCborKey` roots
      into `bmff:item-jumb` when no raw source payload is available
    - `compile_prepared_bundle_bmff(...)`,
      `emit_prepared_bundle_bmff(...)`,
      `emit_prepared_bundle_bmff_compiled(...)`, and
      `emit_prepared_transfer_compiled(..., BmffTransferEmitter&)` provide
      the reusable item/property-emitter path
    - the shared package-batch persistence/replay layer can own and hand off
      those stable BMFF item and property payload bytes
    - OpenMeta also supports a bounded BMFF edit path for targets without a
      foreign top-level `meta` box, or with a prior OpenMeta-authored
      metadata-only `meta` box from the same bounded contract. It preserves
      non-`meta` top-level BMFF boxes and replaces the OpenMeta-authored
      metadata-only `meta` box with the prepared BMFF items/properties.
    - For targets with a parseable foreign top-level `meta` box, OpenMeta can
      merge, replace, or strip bounded Exif/XMP/JUMBF/C2PA items by extending
      `iinf`, `iloc`, `idat`, and `iref` with `cdsc` references to the primary
      item. This path requires a single parseable `iinf`, `iloc` version
      0/1/2, `pitm`, and at most one `idat`. Inserted item IDs can use the
      32-bit item-id space: supported `iloc` version 0/1 graphs are upgraded
      to output `iloc` version 2 when needed, and OpenMeta emits wider
      `infe`/`iref` records for inserted items that exceed 16 bits. Retained
      foreign item locations support construction method 0 file offsets and
      construction method 1 `idat` extents with data reference index 0.
      Retained construction method 2 item-reference extents are supported when
      `iref` `iloc` references are parseable by explicit extent index or
      reference order and referenced items are also retained with supported
      local locations; missing references, removed referenced items, external
      data references, and other construction methods fail safely.
      Bounded ICC transfer removes prior ICC `colr/prof` and `colr/rICC`
      properties from `iprp/ipco`, compacts/remaps existing `ipma`
      associations, appends the transferred `colr/prof` property, and
      associates it with the primary item and any retained item that previously
      referenced a replaced ICC property while preserving the prior essential
      association bit. Arbitrary BMFF scene/property graph rewrite remains
      unsupported.
    - CLI/Python `metatransfer` wrappers expose both BMFF summaries and this
      bounded edit path; `--target-heif`, `--target-avif`, and `--target-cr3`
      now accept `--source-meta PATH` plus `--output PATH` for metadata
      transfer onto an existing BMFF target file
    - the same bounded BMFF edit contract now also participates in the core /
      file-helper C2PA signer path:
      `build_prepared_c2pa_sign_request(...)`,
      `build_prepared_c2pa_sign_request_binding(...)`,
      `validate_prepared_c2pa_sign_result(...)`, and
      `apply_prepared_c2pa_sign_result(...)` can reconstruct rewrite binding
      from preserved source ranges plus one prepared metadata-only `meta` box
      and can stage validated signed logical C2PA back as `bmff:item-c2pa`
      before bounded BMFF edit
    - CLI/Python signer-input options now support JPEG, JXL, and bounded
      BMFF targets; the legacy option name `--jpeg-c2pa-signed` is kept for
      compatibility even when the target is JXL or BMFF
  - `TransferProfile` now uses explicit `TransferPolicyAction` values for
    `makernote`, `jumbf`, and `c2pa`.
  - `PreparedTransferBundle::policy_decisions` records the resolved per-family
    transfer decision during prepare.
  - CLI and Python transfer probes now expose those resolved policy decisions
    directly, and JPEG edit plans report how many existing APP11 JUMBF/C2PA
    segments will be removed during rewrite.
  - C2PA decisions now expose three explicit fields:
    - `TransferC2paMode`
    - `TransferC2paSourceKind`
    - `TransferC2paPreparedOutput`
    so callers can tell whether prepare saw decoded-only C2PA, content-bound
    raw C2PA, or a raw draft invalidation payload, and whether the prepared
    output was dropped, preserved raw, or generated as a draft invalidation.
  - `PreparedTransferBundle::c2pa_rewrite` is the future-facing signer
    contract for `c2pa=rewrite`.
    - It is separate from `policy_decisions`.
    - Current JPEG, JXL, and bounded BMFF prepare fill `state`, `source_kind`,
      matched decoded-entry count, existing carrier segment count, and the
      required signer inputs.
    - Current rewrite prep also emits `content_binding_chunks`, a deterministic
      sequence that describes the rewrite output before any new C2PA payload is
      inserted:
      preserved source ranges plus prepared JPEG segments for JPEG,
      preserved source ranges plus prepared JXL boxes for JXL, or
      preserved source ranges plus one prepared metadata-only `meta` box for
      the bounded BMFF edit path.
    - Current state is usually `SigningMaterialRequired`; it advances to
      `Ready` once an external signed payload is staged back into the bundle.
  - `build_prepared_c2pa_sign_request(...)` derives an explicit external
    signer request from `PreparedTransferBundle::c2pa_rewrite`.
    - It reports carrier route, manifest label, source-range chunk count,
      prepared-segment chunk count, and the full content-binding chunk list.
    - CLI/Python thin wrappers expose this as `c2pa_sign_request`.
  - `build_prepared_c2pa_sign_request_binding(...)` reconstructs the exact
    content-binding byte stream from the request plus the target container
    bytes.
    - It fails closed on stale requests, bad source ranges, and block/size
      mismatches.
    - Current bounded targets are JPEG, JXL, and BMFF.
    - `metatransfer --dump-c2pa-binding` and
      `unsafe_transfer_probe(include_c2pa_binding_bytes=True)` are thin
      wrapper entry points for JPEG, JXL, and bounded BMFF targets.
  - `build_prepared_c2pa_handoff_package(...)` bundles the signer request and
    exact content-binding bytes into one public handoff object.
    - Callers can persist or pass one object to an external signer.
    - Wrappers still use the same core helper instead of rebuilding either
      part on their own.
  - `serialize_prepared_c2pa_handoff_package(...)` and
    `deserialize_prepared_c2pa_handoff_package(...)` persist that handoff
    object as one stable binary package.
  - `build_prepared_c2pa_signed_package(...)` packages the sign request plus
    signer material and returned logical payload for a second persisted
    round-trip object.
  - `serialize_prepared_c2pa_signed_package(...)` and
    `deserialize_prepared_c2pa_signed_package(...)` persist that signed
    package.
  - `validate_prepared_c2pa_sign_result(...)` validates a returned signed
    logical C2PA payload before bundle mutation.
    - It reports payload kind, logical payload size, staged carrier size,
      staged segment count, semantic validation status/reason, and validation
      errors.
    - Current semantic validation requires a manifest, at least one claim,
      at least one signature, linked-signature consistency, no unresolved or
      ambiguous explicit claim references, exactly one manifest for the
      current sign request, `claim_generator` when the request requires
      manifest-builder output, at least one decoded assertion when the
      request requires content binding, the primary signature linking back to
      the prepared primary claim under that same content-binding contract, no
      primary-signature explicit-reference ambiguity under that same request,
      and no multi-signature drift where the primary claim is referenced by
      more than one signature under the current sign request, and no extra
      linked signatures beyond the prepared sign request,
      manifest/claim/signature projection shape under the prepared manifest
      contract, and an exact match between the signer-provided
      `manifest_builder_output` bytes and the primary CBOR manifest payload
      embedded in the returned signed JUMBF.
    - `apply_prepared_c2pa_sign_result(...)` uses the same validation path.
    - Current JPEG validation now also checks that the staged APP11 sequence
      reconstructs the logical payload byte-for-byte, that APP11 sequence
      numbers are contiguous, that repeated APP11 C2PA headers stay
      consistent, and that the logical root type plus BMFF declared size stay
      internally consistent before final emit/write.
    - Current bounded BMFF validation also checks that the staged
      `bmff:item-c2pa` carrier reconstructs the logical payload byte-for-byte
      before bounded BMFF edit applies it.
    - Final JPEG emit/write also validates the prepared APP11 C2PA carrier
      against the bundle's own C2PA contract.
      - `GeneratedDraftUnsignedInvalidation` must carry a draft invalidation
        payload.
      - `SignedRewrite` must carry content-bound C2PA and
        `PreparedTransferBundle::c2pa_rewrite` must already be `Ready`.
      - `Dropped` and `NotPresent` may not leave a prepared APP11 C2PA carrier.
      - Missing required carriers fail before backend bytes are written.
  - `apply_prepared_c2pa_sign_result(...)` is the first bundle-level handoff
    point back from an external signer.
    - It validates the signer request against the current prepared bundle.
    - It requires explicit signer material fields plus a content-bound logical
      C2PA payload.
    - On success it replaces prepared `jpeg:app11-c2pa` blocks or
      `bmff:item-c2pa` items and upgrades the resolved C2PA policy to
      `SignedRewrite` with
      `TransferPolicyReason::ExternalSignedPayload`.
    - CLI/Python thin wrappers expose the validation result as
      `c2pa_stage_validate` and the stage result as `c2pa_stage` for both
      JPEG and bounded BMFF signer-input paths.
  - Current policy resolution for JPEG/TIFF prepare is:
  - MakerNote: `Keep` by default, `Drop` when requested, `Invalidate`
    resolves to `Drop`, and `Rewrite` currently resolves to raw-preserve
    (`Keep`) with a warning.
  - JUMBF:
    - `prepare_metadata_for_target(...)` can now project decoded non-C2PA
      `JumbfCborKey` roots into generic JPEG APP11 JUMBF payloads.
    - The projected path is intentionally bounded:
      ambiguous numeric map keys and decoded-CBOR bool/simple/sentinel
      and large-negative fallback forms are rejected, while tagged CBOR
      values are preserved.
    - `prepare_metadata_for_target_file(...)` can preserve source JUMBF payloads
      for JPEG targets by repacking them into APP11 segments.
    - `append_prepared_bundle_jpeg_jumbf(...)` is the explicit public helper
      for adding one logical raw JUMBF payload to a prepared JPEG bundle, and
      `metatransfer --jpeg-jumbf file.jumbf` is the thin CLI path over it.
  - C2PA:
    - `c2pa=invalidate` on JPEG targets now resolves to a draft unsigned APP11
      C2PA invalidation payload instead of drop-only behavior.
    - The generated draft payload now includes an explicit OpenMeta contract
      marker and contract version in its CBOR map.
    - File-based JPEG prepare can preserve an existing OpenMeta draft
      invalidation payload as raw APP11 C2PA (`TransferC2paMode::PreserveRaw`).
    - `Rewrite` resolves to `Drop` with
      `TransferPolicyReason::SignedRewriteUnavailable` until re-sign support
      exists, while `PreparedTransferBundle::c2pa_rewrite` reports the signer
      prerequisites that would be needed to perform it.
    - OpenMeta still does not sign internally, but it can now stage
      externally signed logical C2PA payloads back into prepared JPEG APP11
      carrier blocks after request validation.
    - `build_prepared_c2pa_handoff_package(...)` and
      `validate_prepared_c2pa_sign_result(...)` are the public handoff and
      pre-stage validation helpers for that external-signer path.
    - `metatransfer` can now dump a persisted handoff package, dump a
      persisted signed package, and load a persisted signed package back into
      the same prepare/validate/apply flow.
  - JPEG edit/rewrite now recognizes existing APP11 JUMBF/C2PA carrier
    segments.
    - Existing C2PA APP11 payloads are dropped automatically when the output
      metadata changes.
    - Existing JUMBF APP11 payloads are removed when the resolved transfer
      policy for JUMBF is `Drop`.
  - C2PA raw preserve still resolves to `Drop` because signed content-bound
    metadata has no safe preserve path without re-sign support.

`thumdump` is preview-only and optimized for batch preview extraction:

```bash
#Positional input / output
./build/thumdump input.jpg preview.jpg

#Explicit input / output
./build/thumdump -i input.jpg -o preview.jpg

#Batch mode
./build/thumdump --out-dir previews --first-only input1.jpg input2.cr2

#If multiple previews exist, --out gets auto - suffixed:
#preview_1.jpg, preview_2.jpg, ...
./build/thumdump input.arq preview.jpg
```

### Resource Budgets (Draft)

OpenMeta tools now default to **no hard file-size cap** (`--max-file-bytes 0`).
Resource control is expected to come from parser/decode budgets:

- `metaread` / `metavalidate` / `metadump` / `metatransfer`:
  - `--max-payload-bytes`, `--max-payload-parts`
  - `--max-exif-ifds`, `--max-exif-entries`, `--max-exif-total`
  - `--max-exif-value-bytes`, `--max-xmp-input-bytes`
- `metadump` / `thumdump` preview scan:
  - `--max-preview-ifds`, `--max-preview-total`, `--max-preview-bytes`

This policy surface is intentionally marked draft and may be refined.

## Code Organization (EXIF + MakerNotes)

- Core EXIF/TIFF decoding: `src/openmeta/exif_tiff_decode.cc`
- Normalized DNG/RAW CCM query surface: `src/include/openmeta/ccm_query.h`,
  `src/openmeta/ccm_query.cc` (`collect_dng_ccm_fields(...)`)
  with DNG-oriented validation diagnostics (`CcmIssue`) in warning mode and
  non-finite numeric field rejection.
  Current warning taxonomy also includes practical checks such as
  `invalid_illuminant_code`, `white_xy_out_of_range`, and unusually large
  matrix-like field counts.
- ICC tag interpretation helpers: `src/include/openmeta/icc_interpret.h`,
  `src/openmeta/icc_interpret.cc` (`icc_tag_name(...)`,
  `interpret_icc_tag(...)` for `desc`/`text`/`sig `/`mluc`/`dtim`/`view`/`meas`/`chrm`/`sf32`/`uf32`/`ui08`/`ui16`/`ui32`/`mft1`/`mft2`/`mAB`/`mBA`/`XYZ `/`curv`/`para`,
  plus `format_icc_tag_display_value(...)` for shared CLI/Python rendering)
- ISO-BMFF (HEIF/AVIF/CR3) container-derived fields: `src/openmeta/bmff_fields_decode.cc`
  - Emitted during `simple_meta_read(...)` as `MetaKeyKind::BmffField` entries.
  - Current fields: `ftyp.*` brand codes/names and compatible-brand counts,
    primary item properties
    (`meta.primary_item_id`, `primary.width`, `primary.height`,
    `primary.rotation_degrees`, `primary.mirror` from `pitm` + `iprp/ipco
    ispe/irot/imir` + `ipma`), primary `colr` summaries
    (`primary.color_type`, `primary.color_type_name`,
    `primary.nclx_colour_primaries`,
    `primary.nclx_transfer_characteristics`,
    `primary.nclx_matrix_coefficients`, `primary.nclx_full_range_flag`, and
    `primary.color_profile_bytes` for bounded ICC profile carriers), `ipco`
    property-container summaries (`ipco.property_count`,
    `ipco.known_property_count`, `ipco.unknown_property_count`, and
    per-known-type counts), `ipma` item-property association rows
    (`ipma.association_count`, `ipma.item_id`, `ipma.property_index`,
    `ipma.essential`, `ipma.property_type`, `ipma.property_type_name`) plus
    per-property rollups such as `ipma.ispe.association_count`,
    `ipma.ispe.primary_association_count`, and
    `ipma.irot.essential_count`,
    item-info rows from `iinf/infe`
    (`item.info_count`, `item.id`, `item.type`,
    `item.type_name`, `item.semantic`, `item.name`, `item.content_type`,
    `item.content_encoding`, `item.uri_type`, and `item.semantic_*_count`
    aggregate rows for common item roles; emitted even when `meta` has no
    `pitm`, plus `primary.item_type`, `primary.item_type_name`,
    `primary.item_semantic`, `primary.item_name`, `primary.content_type`,
    `primary.content_encoding`, `primary.uri_type` aliases when `pitm` is
    present), bounded `iref.*` relation fields (`ref_type`, `ref_type_name`,
    `from_item_id`, `to_item_id`, `edge_count`), typed derived relation rows
    (`iref.auxl.*`, `iref.dimg.*`, `iref.thmb.*`, `iref.cdsc.*`, and other safe
    ASCII FourCC relation families), direction-correct endpoint role and
    named item-id aliases (`from_role`, `to_role`, auxiliary/master,
    derived/source, thumbnail/master, and descriptive/described item IDs),
    per-type relation counters
    (`iref.<type>.edge_count`) and per-type unique source/target counters
    (`iref.<type>.from_item_unique_count`,
    `iref.<type>.to_item_unique_count`), per-type graph-summary aliases
    (`iref.graph.<type>.edge_count`,
    `iref.graph.<type>.from_item_unique_count`,
    `iref.graph.<type>.to_item_unique_count`), typed relation item summaries
    (`iref.<type>.item_count`, `iref.<type>.item_id`,
    `iref.<type>.item_out_edge_count`, `iref.<type>.item_in_edge_count`),
    relation-graph summaries (`iref.item_count`,
    `iref.from_item_unique_count`, `iref.to_item_unique_count`, row-wise
    `iref.item_id` + `iref.item_out_edge_count` +
    `iref.item_in_edge_count`), separate primary inbound derived-item and
    outbound derived-source summaries, bounded primary-linked image-role rows
    (`primary.linked_item_role_count`, row-wise `primary.linked_item_id` +
    `primary.linked_item_type` + `primary.linked_item_type_name` +
    `primary.linked_item_name` + `primary.linked_item_semantic` +
    `primary.linked_item_role` when `iinf/infe` data exists), compact
    primary sidecar summaries with unique linked-node counts
    (`primary.sidecar_count`,
    `primary.has_metadata_sidecar`, `primary.metadata_sidecar_count`,
    `primary.has_image_sidecar`, `primary.image_sidecar_count`, and
    per-role `primary.*_sidecar_count` fields), compact primary-scene
    summaries (`primary.scene_primary_item_count`,
    `primary.scene_linked_item_count`, `primary.scene_node_count`,
    `primary.scene_edge_count`, `primary.scene_*_node_count`, and
    `primary.scene_*_edge_count`), whole-scene policy summaries
    (`scene.item_count`, `scene.known_item_count`, `scene.image_node_count`,
    `scene.metadata_node_count`, `scene.content_bound_metadata_node_count`,
    `scene.edge_count`, `scene.item_group_count`, bounded item-group semantic,
    entity-index, and entity-role rows for `altr`, `ster`, and `pymd`,
    `scene.has_content_bound_metadata`,
    `scene.content_bound_metadata_policy`, and
    `scene.multi_image_candidate`, `scene.multi_image_policy`),
    graph-component summaries
    (`scene.graph_node_count`, `scene.graph_component_count`,
    `scene.graph_image_component_count`,
    `scene.graph_multi_image_component_count`,
    `scene.graph_content_bound_metadata_component_count`,
    `scene.graph_observed_edge_count`,
    per-component rows (`scene.component.index`, `scene.component.role`,
    `scene.component.item_id`, known/unknown and semantic `*_node_count`
    fields, `scene.component.isolated`, typed auxiliary/derived/thumbnail/
    content-description `*_edge_count` fields, and alpha/depth/disparity/matte
    auxiliary edge counts),
    `scene.primary_graph_component_node_count`,
    `scene.primary_graph_component_*_node_count`,
    `scene.primary_graph_component_edge_count`,
    `scene.primary_graph_component_has_content_bound_metadata`,
    `scene.primary_graph_component_multi_image_candidate`,
    `scene.primary_graph_component_multi_image_policy`, and
    `scene.primary_graph_component_metadata_policy`), and
    bounded derived-image construction rows (`derived_image.*`) for `grid`,
    `iovl`, and `iden`, including ordered source IDs, descriptor/source/count
    validity, grid rows/columns/tile coordinates/output dimensions, overlay
    canvas/background/signed offsets, identity sources, descriptor-reference
    depth, graph cycle/self-reference/missing-source/depth/truncation validity,
    and primary aliases. Descriptor reads use complete method-0 file extents,
    method-1 `idat` extents, or bounded recursive method-2 item-offset chains.
    External data references other than tiled-image `deti` URL state and
    incomplete extents remain structural-only. `tili` items expose bounded
    `tilC` version-0 tile dimensions, up to eight extra dimensions, required
    single-`ispe`/single-`tilC` relationship checks, ceil-divided tile grids,
    overflow-checked expected tile counts, mapped `dref`/`deti` state,
    internal `tile_item_type` and nested `tipa` property associations,
    external URL components, logical offset-table rows, explicit or
    sequentially inferred sizes, empty tiles, and separate
    core/layout/complete validity. Offset validation scans at most 262144
    entries and emits at most 64 rows.
    Also emits
    `auxC`-based aux semantics (`aux.item_count`, `aux.item_id`,
    `aux.semantic`, `aux.type`, `aux.subtype_hex`, `aux.subtype_kind`,
    `aux.subtype_text`, `aux.subtype_uuid`, `aux.subtype_u32`,
    `aux.subtype_u64`, `aux.alpha_count`, `aux.depth_count`,
    `aux.disparity_count`, `aux.matte_count`, `primary.auxl_count`,
    `primary.auxl_semantic`, `primary.depth_count`, `primary.depth_item_id`,
    `primary.alpha_count`, `primary.alpha_item_id`,
    `primary.disparity_count`, `primary.disparity_item_id`,
    `primary.matte_count`, `primary.matte_item_id`, `primary.dimg_count`,
    `primary.dimg_item_id`, `primary.thmb_count`, `primary.thmb_item_id`,
    `primary.cdsc_count`, `primary.cdsc_item_id`, ...). Full multi-image scene
    modeling beyond these bounded aggregate surfaces is still follow-up work.
  - `auxC` subtype interpretation now includes `ascii_z` and `u64be` kinds in addition to earlier numeric/FourCC/UUID/ASCII forms.
  - Parsing is intentionally bounded (depth/box count caps) and ignores unknown properties.
  - `derived_image.construction` and primary construction aliases resolve to
    the source-bound `container_graph.derived_image_construction` concept.
    Compatible-file diagnostics keep this evidence; rendered-image diagnostics
    use the dedicated `drop.derived_image_construction` message token.
  - `tiled_image.configuration` resolves separately to the source-bound
    `container_graph.tiled_image_configuration` concept. Rendered-image
    diagnostics use `drop.tiled_image_configuration`.
- JUMBF/C2PA decode (draft phase-3): `src/openmeta/jumbf_decode.cc`
  - Routed from container scan blocks tagged as `ContainerBlockKind::Jumbf`
    (BMFF `jumb`/C2PA hints and JXL `jumb` boxes).
  - Emits structural fields as `MetaKeyKind::JumbfField` (`box.*`, `c2pa.*`,
    including JUMBF labels from parsed `jumd` boxes) and decoded CBOR keys as
    `MetaKeyKind::JumbfCborKey` (`*.cbor.*`).
  - Current CBOR path supports bounded definite and indefinite forms, with
    composite-key fallback naming (`k{map_index}_{
    major}`) and broader scalar
    decode coverage (simple values + half/float/double bit-preserving paths).
  - Draft semantic projection emits stable `c2pa.semantic.*` fields
    (`manifest_present`, `active_manifest_present`,
    `active_manifest_count`, `active_manifest.prefix`, `claim_present`,
    `assertion_present`, `ingredient_present`, `signature_present`,
    `assertion_key_hits`, `ingredient_key_hits`, `cbor_key_count`,
    `signature_count`,
    `claim_generator` when ASCII-safe), plus draft per-claim fields
    (`claim_count`, `assertion_count`, `ingredient_count`,
    `claim.{i}.prefix`,
    `claim.{i}.assertion_count`, `claim.{i}.key_hits`,
    `claim.{i}.ingredient_count`,
    `claim.{i}.signature_count`, `claim.{i}.signature_key_hits`,
    `claim.{i}.claim_generator` when ASCII-safe), per-assertion fields
    (`claim.{i}.assertion.{j}.prefix`, `claim.{i}.assertion.{j}.key_hits`),
    and per-ingredient fields
    (`claim.{i}.ingredient.{j}.prefix`,
    `claim.{i}.ingredient.{j}.key_hits`,
    `claim.{i}.ingredient.{j}.title`,
    `claim.{i}.ingredient.{j}.relationship`,
    `claim.{i}.ingredient.{j}.thumbnail_url` when ASCII-safe), plus bounded
    ingredient summary counts
    (`ingredient_relationship_count`, `ingredient_thumbnail_url_count`,
    `ingredient_claim_count`,
    `ingredient_claim_with_signature_count`,
    `ingredient_claim_referenced_by_signature_count`,
    per-claim linked-ingredient summary fields such as
    `claim.{i}.linked_ingredient_signature_count`,
    `claim.{i}.linked_ingredient_title_count`,
    `claim.{i}.linked_ingredient_relationship_count`,
    `claim.{i}.linked_ingredient_thumbnail_url_count`,
    `claim.{i}.linked_ingredient_relationship_kind_count`,
    and explicit-reference split variants like
    `claim.{i}.linked_ingredient_explicit_reference_title_count`,
    plus aggregate linked-claim topology counts like
    `ingredient_linked_claim_count` and
    `ingredient_linked_claim_direct_source_count`,
    `ingredient_manifest_count`,
    `ingredient_signature_count`,
    `ingredient_linked_signature_count`,
    `ingredient_linked_direct_claim_count`,
    `ingredient_linked_cross_claim_count`,
    `ingredient_linked_signature_direct_source_count`,
    `ingredient_linked_signature_cross_source_count`,
    `ingredient_linked_signature_mixed_source_count`,
    `ingredient_linked_signature_direct_title_count`,
    `ingredient_linked_signature_cross_title_count`,
    `ingredient_linked_signature_direct_relationship_count`,
    `ingredient_linked_signature_cross_relationship_count`,
    `ingredient_linked_signature_direct_thumbnail_url_count`,
    `ingredient_linked_signature_cross_thumbnail_url_count`,
    `ingredient_linked_signature_title_count`,
    `ingredient_linked_signature_relationship_count`,
    `ingredient_linked_signature_relationship_kind_count`,
    `ingredient_linked_signature_relationship.<value>_count`,
    `ingredient_linked_signature_thumbnail_url_count`,
    `ingredient_linked_signature_explicit_reference_direct_title_count`,
    `ingredient_linked_signature_explicit_reference_cross_title_count`,
    `ingredient_linked_signature_explicit_reference_title_count`,
    `ingredient_linked_signature_explicit_reference_relationship_count`,
    `ingredient_linked_signature_explicit_reference_relationship_kind_count`,
    `ingredient_linked_signature_explicit_reference_relationship.<value>_count`,
    `ingredient_linked_signature_explicit_reference_thumbnail_url_count`,
    `ingredient_linked_signature_explicit_reference_direct_claim_count`,
    `ingredient_linked_signature_explicit_reference_cross_claim_count`,
    `ingredient_linked_signature_explicit_reference_direct_source_count`,
    `ingredient_linked_signature_explicit_reference_cross_source_count`,
    `ingredient_linked_signature_explicit_reference_mixed_source_count`,
    plus corresponding `..._unresolved_*` and `..._ambiguous_*` split
    aggregates,
    `ingredient_explicit_reference_signature_count`,
    `ingredient_explicit_reference_unresolved_signature_count`,
    `ingredient_explicit_reference_ambiguous_signature_count`,
    `claim.{i}.ingredient_relationship_count`,
    `claim.{i}.ingredient_thumbnail_url_count`,
    `manifest.{i}.ingredient_relationship_count`,
    `manifest.{i}.ingredient_thumbnail_url_count`,
    `manifest.{i}.ingredient_claim_count`) and path-sanitized
    relationship alias counts such as
    `ingredient_relationship.parentOf_count`, plus
    per-manifest active-state
    fields (`manifest.{i}.is_active`, `manifest.{i}.ingredient_count`),
    plus draft per-claim signature fields
    (`claim.{i}.signature.{k}.prefix`, `claim.{i}.signature.{k}.key_hits`,
    `claim.{i}.signature.{k}.algorithm` when available), plus draft
    per-signature fields
    (`signature_count`, `signature_key_hits`, `signature.{k}.prefix`,
    `signature.{k}.key_hits`, `signature.{k}.algorithm` when available,
    `signature.{k}.reference_key_hits`,
    `signature.{k}.linked_claim_count`,
    `signature.{k}.linked_ingredient_claim_count`,
    `signature.{k}.linked_direct_ingredient_claim_count`,
    `signature.{k}.linked_cross_ingredient_claim_count`,
    `signature.{k}.linked_ingredient_title_count`,
    `signature.{k}.linked_ingredient_relationship_count`,
    `signature.{k}.linked_ingredient_relationship_kind_count`,
    `signature.{k}.linked_ingredient_relationship.<value>_count`,
    `signature.{k}.linked_ingredient_thumbnail_url_count`,
    `signature.{k}.direct_claim_has_ingredients`,
    `signature.{k}.cross_claim_link_count`,
    `signature.{k}.explicit_reference_present`,
    `signature.{k}.explicit_reference_resolved_claim_count`,
    `signature.{k}.explicit_reference_unresolved`,
    `signature.{k}.explicit_reference_ambiguous`,
    `signature.{k}.linked_claim.{m}.prefix`),
    plus reference-link counters
    (`reference_key_hits`, `cross_claim_link_count`,
    `explicit_reference_signature_count`,
    `explicit_reference_unresolved_signature_count`,
    `explicit_reference_ambiguous_signature_count`,
    `claim.{i}.referenced_by_signature_count`),
    and linkage counters (`signature_linked_count`,
    `signature_orphan_count`).
  - Draft verify scaffold (`c2pa.verify.*`) now includes:
    - signature-shape validation (`invalid_signature`) for malformed payloads;
    - OpenSSL-backed cryptographic verification (`verified` /
      `verification_failed`) when a signature entry provides algorithm +
      signing input + public key material (`public_key_der`/`public_key_pem` or
      `certificate_der`).
    - opt-in trusted-chain enforcement via
      `verify_require_trusted_chain` /
      `--c2pa-verify-require-trusted-chain`; without this option, signature
      verification and certificate-chain trust are reported as separate
      signals.
    - COSE_Sign1 support (array or embedded CBOR byte-string forms): extracts
      `alg` from protected headers, reconstructs Sig_structure signing bytes
      when payload is present, extracts `x5chain` from unprotected headers, and
      accepts raw ECDSA signatures (`r||s`) by converting to DER for OpenSSL.
    - detached payload resolution (`payload=null`) using explicit
      reference-linked candidates first (for example `claims[n]` / claim-label
      references in decoded claim/signature fields, scalar index references,
      and indexed array-element reference keys such as `claimRef[0]`), then
      including plural reference-key variants (`references`, `refs`,
      `claim_references`) plus hyphenated variants (`claim-reference`,
      `claim-uri`, `claim-ref-index`), nested URI-like map fields such as
      `references[].href`/`references[].link`, query-style index tokens in URI
      text (`claim-index=...`, `claim_ref=...`), and percent-encoded URI/label
      forms where present. Candidate ordering is deterministic with sorted
      index-like references resolved before sorted label-based references, then
      best-effort fallback probing via claim bytes, single-claim `claims[*]`
      arrays, nearby/nested claim JUMBF boxes, and additional cross-manifest
      candidates. Current tests include conflicting mixed references and
      multi-claim/multi-signature cross-manifest precedence cases, nested
      `references[]` map forms, duplicate overlapping explicit references,
      unresolved explicit-reference no-fallback behavior, conflict/consistent
      `index + claim_reference + href` nested-map ambiguity/consistency cases, and
      percent-encoded query-index URI variants.
    - draft profile checks (`profile_status`/`profile_reason`) from decoded
      `c2pa.semantic.*` shape fields (manifest/claim/signature linkage);
    - draft certificate trust checks (`chain_status`/`chain_reason`) when
      `certificate_der` is present (certificate parse, time validity, and
      OpenSSL trust-store verification).
    Full C2PA/COSE manifest binding and policy validation is still pending.
- GeoTIFF GeoKey decoding (derived keys): `src/openmeta/geotiff_decode.cc`
- Vendor MakerNote decoders: `src/openmeta/exif_makernote_*.cc`
  (Canon, Nikon, Sony, Olympus, Pentax, Casio, Panasonic, Kodak, Ricoh, Samsung, FLIR, etc.)
- Shared internal-only helpers: `src/openmeta/exif_tiff_decode_internal.h`
  (not installed; used to keep vendor logic out of the public API)
- Tests: `tests/makernote_decode_test.cc`
  and `tests/jumbf_decode_test.cc`

When adding or changing MakerNote code, prefer extending the vendor files and
keeping the EXIF/TIFF core container-agnostic. Add/adjust a unit test for any
new subtable or decode path.

Internal helper conventions (used by vendor decoders):
- `read_classic_ifd_entry(...)` + `ClassicIfdEntry`: parse a single 12-byte classic TIFF IFD entry.
- `resolve_classic_ifd_value_ref(...)` + `ClassicIfdValueRef`: compute the value location/size for a classic IFD entry (inline vs out-of-line), using `MakerNoteLayout` + `OffsetPolicy`.
- `MakerNoteLayout` + `OffsetPolicy`: makes "value offsets are relative to X" explicit for vendor formats. `OffsetPolicy` supports both the common unsigned base (default) and a signed base for vendors that require it (eg Canon).
- `ExifContext`: a small, decode-time cache for frequently accessed EXIF values (avoids repeated linear scans of `store.entries()`).
- MakerNote tag-name tables are generated from `registry/exif/makernotes/*.jsonl` and looked up via binary search (`exif_makernote_tag_names.cc`).
- Canonical EXIF names stay context-free via `exif_tag_name(...)`. When corpus
  compatibility requires a decode-time alias split, stamp the variant on the
  `Entry` provenance and resolve it only on explicit display surfaces through
  `exif_entry_name(..., ExifTagNamePolicy::ExifToolCompat)`.
- Photoshop IRB stays lossless at the raw-resource layer (`PhotoshopIrb`).
  Add interpreted IRB fields only for fixed-layout resources, bounded
  descriptor-header summaries, or bounded scalar/class/alias/reference
  descriptor item bodies, and emit them as separate `PhotoshopIrbField`
  entries instead of weakening the raw payload surface. The current bounded
  interpreted subset includes `ResolutionInfo`,
  `AlphaChannelsNames`, `DisplayInfo`, `PStringCaption`, `BorderInformation`,
  `BackgroundColor`, `Photoshop2Info`, `Photoshop2ColorTable`, `VersionInfo`,
  `PrintFlags`, print-flag byte fields, `EffectiveBW`, `QuickMaskInfo`,
  `RawImageMode`, `TargetLayerID`, `LayersGroupInfo`, `JPEG_Quality`,
  `GridGuidesInfo`, legacy halftone/transfer/duotone/EPS byte summaries,
  `CopyrightFlag`, `URL`, `GlobalAngle`, `SpotHalftone`, `JumpToXPEP`,
  `ColorSamplersResource`, `ColorSamplersResource2`, `Watermark`,
  `ICC_Untagged`, `EffectsVisible`, `IDsBaseValue`, `UnicodeAlphaNames`,
  `IndexedColorTableCount`, `TransparentIndex`, `GlobalAltitude`,
  `SliceInfo`, `WorkflowURL`, `AlphaIdentifiers`, `URL_List`,
  `IPTCDataBytes`, `IPTCDigest`, `PrintScaleInfo`, `PixelInfo`,
  `AutoSaveFilePath`, `AutoSaveFormat`, `XMLData`, `ImageReadyVariables`,
  `ImageReadyDataSets`, working-path and numbered clipping-path record
  summaries, descriptor-header, scalar/class/alias/enum, bounded `obj `
  property/class/enumerated/offset/identifier/index/name reference streams,
  and bounded nested list/object summaries for
  `LayerComps`, `MeasurementScale`, `HDRToningInfo`, `PrintInfo`,
  `TimelineInfo`, `SheetDisclosure`, `OnionSkins`, `CountInfo`,
  `PrintInfo2`, `PrintStyle`,
  `PathSelectionState`, and `OriginPathInfo`, `PhotoshopBGRThumbnail`,
  `PhotoshopThumbnail`, `LayerSelectionIDs`, `LayerGroupsEnabledID`,
  `ChannelOptions`, `PrintFlagsInfo`, `ClippingPathName`,
  `MacintoshPrintInfo`, `MacintoshNSPrintInfo`, `WindowsDEVMODE`,
  `AlternateDuotoneColors`, `AlternateSpotColors`, ObsoletePhotoshopTag1,
  ObsoletePhotoshopTag2, and ObsoletePhotoshopTag3.
- Descriptor `obj ` traversal is capped at 64 reference items per value and
  128 per descriptor. Unknown, incomplete, or excess items stop traversal,
  set `DescriptorItemParseTruncated`, and never emit a partial reference item.
- Legacy 8-bit Photoshop text stays opt-in and explicit. The IRB decoder
  exposes a bounded `PhotoshopIrbStringCharset` policy and currently uses it
  only for `AlphaChannelsNames`, `PStringCaption`, and `ClippingPathName`,
  defaulting to Latin for ExifTool-compatible behavior instead of guessing
  from bytes.
- `ChannelOptions` stays bounded and explicit: emit one count row, then one
  `ChannelIndex` row plus the channel fields in stable order for each 13-byte
  record instead of inventing dynamic field names.
- `PrintFlagsInfo` is bounded to the stable `exiv2`-documented 10-byte layout:
  version, center-crop flag, bleed-width value, and bleed-width scale.
- GeoTIFF key-name table is generated from `registry/geotiff/keys.jsonl` and looked up via binary search (`geotiff_key_names.cc`).

## Tests (GoogleTest)

Requirements:
- A GoogleTest package that provides `GTest::gtest_main` (or `GTest::Main`).

Note: if your GoogleTest was built against `libc++` (common with Clang),
build OpenMeta against the same C++ standard library. Otherwise you may see
link errors involving `std::__1` vs `std::__cxx11`.

If external test tools were built with `libc++` and need a custom runtime
lookup path, set `OPENMETA_TEST_RUNTIME_LIBRARY_PATH` at configure time. This
is applied to CTest-launched tests through `LD_LIBRARY_PATH` on Linux and
`DYLD_LIBRARY_PATH` on macOS.

Build + run:
```bash
cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/f/UBSd \
  -DOPENMETA_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Optional CLI integration test for preview index suffixing:
```bash
cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/f/UBSd \
  -DOPENMETA_BUILD_TESTS=ON \
  -DOPENMETA_MULTI_PREVIEW_SAMPLE=/path/to/file_with_multiple_previews
cmake --build build-tests
ctest --test-dir build-tests -R openmeta_cli_preview_index --output-on-failure
```

If `OPENMETA_MULTI_PREVIEW_SAMPLE` is not set (or the file is missing),
`openmeta_cli_preview_index` is skipped.

Fast public smoke gate for `metavalidate` (self-contained, no corpus needed):
```bash
cmake --build build-tests --target openmeta_gate_metavalidate_smoke
ctest --test-dir build-tests -R openmeta_cli_metavalidate_smoke --output-on-failure
```

Fast public smoke gate for `metaread` safe-text placeholder behavior:
```bash
cmake --build build-tests --target openmeta_gate_metaread_safe_text_smoke
```

Fast public smoke gate for `metaread` Photoshop IRB field visibility:
```bash
cmake --build build-tests --target openmeta_gate_metaread_photoshop_irb_smoke
ctest --test-dir build-tests -R openmeta_cli_metaread_photoshop_irb_smoke --output-on-failure
```

Fast public smoke gate for `metatransfer` thin wrapper behavior:
```bash
cmake --build build-tests --target openmeta_gate_metatransfer_smoke
ctest --test-dir build-tests -R openmeta_cli_metatransfer_smoke --output-on-failure
```

Fast public smoke gate for Python `openmeta.transfer_probe` thin wrapper
behavior (requires `-DOPENMETA_BUILD_PYTHON=ON`):
```bash
cmake --build build-tests --target openmeta_gate_python_transfer_probe_smoke
ctest --test-dir build-tests -R openmeta_python_transfer_probe_smoke --output-on-failure
```

Fast public smoke gate for Python `openmeta.python.metatransfer` edit mode
behavior (requires `-DOPENMETA_BUILD_PYTHON=ON`):
```bash
cmake --build build-tests --target openmeta_gate_python_metatransfer_edit_smoke
ctest --test-dir build-tests -R openmeta_python_metatransfer_edit_smoke --output-on-failure
```

Stronger transfer release gate:
- in a non-Python test tree it runs:
  - `MetadataTransferApi.*`
  - `XmpDump.*`
  - `ExrAdapter.*`
  - `DngSdkAdapter.*`
  - `openmeta_cli_metatransfer_smoke`
- in a Python-enabled test tree it also runs:
  - `openmeta_python_transfer_probe_smoke`
  - `openmeta_python_metatransfer_edit_smoke`

Build + run:
```bash
cmake --build build-tests --target openmeta_gate_transfer_release
ctest --test-dir build-tests -R openmeta_transfer_release_gate --output-on-failure
```

The external image-usability gate can also use existing BMFF target files when
local tools cannot create them:
`OPENMETA_BMFF_HEIF_TEST_TARGET`, `OPENMETA_BMFF_AVIF_TEST_TARGET`, and
`OPENMETA_BMFF_CR3_TEST_TARGET`. Configured targets exercise the ICC property,
XMP item, MakerNote, and EXIF image-property transfer/read-back paths. For real
configured targets, the gate infers target-owned image dimensions, channel
count, bit depth, sample format, and photometric layout before transfer, so it
does not intentionally write mismatched image geometry. The configured XMP
assertion is based on OpenMeta's BMFF
summary; ExifTool title validation is also applied for formats where ExifTool
exposes the generic BMFF XMP item. ExifTool is also used for BMFF EXIF/ICC
reader checks when available. If the local `oiiotool` build cannot decode a
configured BMFF target after rewrite, `OPENMETA_FFMPEG_EXECUTABLE` can provide
the decode fallback.

ExifTool is an optional external validation tool in these tests, not an
OpenMeta runtime dependency. Keep it patched when running validation against
untrusted files. This matters especially on macOS, where older ExifTool
releases have had metadata-write command-injection issues in their own
platform-specific tooling.

The public GitHub Actions workflow `.github/workflows/ci.yml` runs two Linux
variants of these public release gates:
- self-contained non-Python, non-DNG-SDK
- Python-enabled, non-DNG-SDK, with `nanobind` installed into the CI
  interpreter via `pip`

Read release gate:
- core self-contained decode and adapter suites such as:
  - `ContainerScan.*`
  - `ContainerPayload.*`
  - `ExifTiffDecode.*`
  - `SimpleMetaRead.*`
  - `XmpDecodeTest.*`
  - `JumbfDecode.*`
  - `OcioAdapter.*`
  - `ExrAdapter.*`
  - `ValidateFile.*`

Build + run:
```bash
cmake --build build-tests --target openmeta_gate_read_release
ctest --test-dir build-tests -R openmeta_read_release_gate --output-on-failure
```

CLI release gate:
- self-contained non-transfer CLI smokes:
  - `openmeta_cli_metaread_safe_text_smoke`
  - `openmeta_cli_metaread_photoshop_irb_smoke`
  - `openmeta_cli_metavalidate_smoke`
  - `openmeta_cli_numeric_parse_smoke`

Build + run:
```bash
cmake --build build-tests --target openmeta_gate_cli_release
ctest --test-dir build-tests -R openmeta_cli_release_gate --output-on-failure
```

Coverage note:
- Public tree tests focus on deterministic unit/fuzz/smoke behavior.
- Corpus-scale compare/baseline workflows are external to the public tree and
  should be run in your CI/release validation pipeline.

## libFuzzer Targets

Requirements:
- Clang with libFuzzer support.

Notes:
- On Linux, Clang's bundled libFuzzer runtime is typically built against
  `libstdc++`. When `OPENMETA_USE_LIBCXX=ON`, OpenMeta keeps tests/tools on
  `libc++` but builds fuzz targets against `libstdc++` to match libFuzzer.
- libFuzzer treats metadata as untrusted input; always run under sanitizers
  and with explicit size limits.

Build + run (example 5s smoke run):
```bash
cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DOPENMETA_BUILD_FUZZERS=ON
cmake --build build-fuzz
ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_exif_tiff_decode -max_total_time=5
```

Corpus runs (seed corpora)
--------------------------

If you pass corpus directories to libFuzzer, it treats the **first** directory
as the main corpus and may add/reduce files there. To avoid modifying your seed
corpus directories, use an empty output directory first:

```bash
mkdir -p build-fuzz/_corpus_out
ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_container_scan \
  build-fuzz/_corpus_out \
  /path/to/seed-corpus-a /path/to/seed-corpus-b \
  -runs=1000
```

Public seed corpus is available in-tree:

```bash
mkdir -p build-fuzz/_corpus_out
ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_container_scan \
  build-fuzz/_corpus_out \
  tests/fuzz/corpus/container_scan \
  -runs=1000
```

The `container_scan` seed set includes BMFF `iloc` method-2 edge cases:
- valid `iref` v1 (`32-bit` item-id) resolution,
- missing `iref` mapping,
- out-of-range explicit `extent_index`,
- `idx_size=0` extent/reference mismatch fallback behavior.

## FuzzTest

Requirements:
- A FuzzTest package that provides `fuzztest::fuzztest` and `fuzztest::fuzztest_gtest_main`.

Build + run:
```bash
cmake -S . -B build-fuzztest -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/f/UBSd \
  -DOPENMETA_BUILD_FUZZTEST=ON -DOPENMETA_FUZZTEST_FUZZING_MODE=ON
cmake --build build-fuzztest
ASAN_OPTIONS=detect_leaks=0 ./build-fuzztest/openmeta_fuzztest_metastore --list_fuzz_tests
ASAN_OPTIONS=detect_leaks=0 ./build-fuzztest/openmeta_fuzztest_metastore --fuzz=MetaStoreFuzz.meta_store_op_stream --fuzz_for=5s
```

## Python (nanobind)

Requirements:
- Python `>= 3.9` + development headers/libraries
- `nanobind` installed as a CMake package (findable via `CMAKE_PREFIX_PATH`)

Build:
```bash
cmake -S . -B build-py -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/mnt/f/UBS \
  -DOPENMETA_BUILD_PYTHON=ON -DOPENMETA_BUILD_TOOLS=OFF
cmake --build build-py
PYTHONPATH=build-py/python python3 -c "import openmeta; print(openmeta.read('file.jpg').entry_count)"
```

Notes:
- `openmeta.read(...)` releases the Python GIL while doing file I/O and decode,
  so it can be called from multiple Python threads in parallel (useful for corpus
  comparisons).
- `openmeta.validate(...)` is the library-backed validation API used by
  `openmeta.python.metavalidate`; it returns decode/CCM issue summaries without
  Python-side validation logic.
- Python bindings are thin wrappers over C++ decode logic. Resource/safety
  limits should be configured via `openmeta.ResourcePolicy` and passed to
  `openmeta.read(...)`.

Example policy usage:
```bash
PYTHONPATH=build-py/python python3 - <<'PY'
import openmeta
policy = openmeta.ResourcePolicy()
policy.max_file_bytes = 0
policy.exif_limits.max_total_entries = 200000
doc = openmeta.read("file.jpg", policy=policy)
print(doc.entry_count)
PY
```

C++ policy setup:
```cpp
#include "openmeta/resource_policy.h"

openmeta::OpenMetaResourcePolicy policy
    = openmeta::recommended_resource_policy();
policy.jumbf_limits.max_box_depth = 24;  // optional override
```

JUMBF preflight depth estimate (before full decode):
```cpp
#include "openmeta/jumbf_decode.h"

const openmeta::JumbfStructureEstimate est
    = openmeta::measure_jumbf_structure(bytes, policy.jumbf_limits);
if (est.status == openmeta::JumbfDecodeStatus::LimitExceeded) {
    // reject or route to stricter handling
}
```

Other preflight estimate entry points follow the same limit model:
```cpp
#include "openmeta/container_scan.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/exr_decode.h"
#include "openmeta/icc_decode.h"
#include "openmeta/iptc_iim_decode.h"
#include "openmeta/jumbf_decode.h"
#include "openmeta/photoshop_irb_decode.h"
#include "openmeta/xmp_decode.h"

const openmeta::ScanResult scan_est
    = openmeta::measure_scan_auto(file_bytes);
const openmeta::ExifDecodeResult exif_est
    = openmeta::measure_exif_tiff(exif_bytes, exif_options);
const openmeta::XmpDecodeResult xmp_est
    = openmeta::measure_xmp_packet(xmp_bytes, xmp_options);
const openmeta::IccDecodeResult icc_est
    = openmeta::measure_icc_profile(icc_bytes, icc_options);
const openmeta::IptcIimDecodeResult iptc_est
    = openmeta::measure_iptc_iim(iptc_bytes, iptc_options);
const openmeta::PhotoshopIrbDecodeResult irb_est
    = openmeta::measure_photoshop_irb(irb_bytes, irb_options);
const openmeta::ExrDecodeResult exr_est
    = openmeta::measure_exr_header(exr_bytes, exr_options);
const openmeta::JumbfDecodeResult jumbf_est
    = openmeta::measure_jumbf_payload(jumbf_bytes, jumbf_options);
```

Example scripts (repo tree):
```bash
PYTHONPATH=build-py/python python3 -m openmeta.python.openmeta_stats file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metaread file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metavalidate file.dng
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump file.jpg output.xmp
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump --format portable file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump --format portable --portable-exiftool-gpsdatetime-alias file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump --format portable --c2pa-verify --c2pa-verify-backend auto file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump --format portable --portable-include-existing-xmp --xmp-sidecar file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --target-jpeg target.jpg -o edited.jpg source.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --target-tiff target.tif --dry-run source.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --jpeg-c2pa-signed signed_c2pa.jumb --c2pa-manifest-output manifest.bin --c2pa-certificate-chain chain.bin --c2pa-key-ref signer-key --c2pa-signing-time 2026-03-09T00:00:00Z -o edited.jpg input_with_c2pa.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --c2pa-policy rewrite --dump-c2pa-handoff handoff.omc2ph input_with_c2pa.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --jpeg-c2pa-signed signed_c2pa.jumb --c2pa-manifest-output manifest.bin --c2pa-certificate-chain chain.bin --c2pa-key-ref signer-key --c2pa-signing-time 2026-03-09T00:00:00Z --dump-c2pa-signed-package signed.omc2ps input_with_c2pa.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --load-c2pa-signed-package signed.omc2ps --target-jpeg target.jpg -o edited.jpg input_with_c2pa.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --dump-transfer-payload-batch payloads.omtpld input.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --load-transfer-payload-batch payloads.omtpld
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --dump-transfer-package-batch package.omtpkg input.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --load-transfer-package-batch package.omtpkg
```

## Python Wheel

Requirements:
- `scikit-build-core` installed in your Python environment.
- A wheel builder: `pip` (recommended) or `uv` (works even if your venv has no `pip`).

Build:
```bash
python3 -m pip wheel . -w dist --no-deps
```
Or using `uv`:
```bash
uv --no-cache build --wheel --no-build-isolation -o dist -p "$(command -v python3)" .
```
After installing the wheel, example modules are available as:
```bash
python3 -m openmeta.python.openmeta_stats file.jpg
python3 -m openmeta.python.metaread file.jpg
python3 -m openmeta.python.metatransfer file.jpg
```
Or via CMake:
```bash
cmake -S . -B build-wheel -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOPENMETA_BUILD_WHEEL=ON \
  -DOPENMETA_PYTHON_EXECUTABLE=/path/to/venv/bin/python3
cmake --build build-wheel --target openmeta_wheel
```

The CMake wheel target and install-time wheel script forward the active compiler
flags, selected Python paths, `OPENMETA_USE_LIBCXX`, and optional feature
toggles into the nested scikit-build configure step. This keeps wheel ABI
choices aligned with the outer CMake build, including libc++-based Linux builds.
Package lookup hints are forwarded as well. Prefer `CMAKE_PREFIX_PATH` for a
complete dependency prefix. If nanobind was packaged with an external
`tsl-robin-map` dependency and CMake cannot resolve it from that prefix, pass
`-Dtsl-robin-map_DIR=/path/to/share/cmake/tsl-robin-map`; the wheel configure
inherits that explicit package directory.

When `OPENMETA_BUILD_WHEEL=ON`, `cmake --install` also builds a wheel and copies
it into `${CMAKE_INSTALL_PREFIX}/share/openmeta/wheels` (and also copies the
Python helper scripts `metaread.py`, `metavalidate.py`, `metadump.py`, `metatransfer.py`,
and `openmeta_stats.py`
into the same directory):
```bash
cmake --install build-wheel --prefix /tmp/openmeta-install
ls /tmp/openmeta-install/share/openmeta/wheels
```

If you are building offline (or want strict control of the build environment),
install `scikit-build-core` into your Python environment and enable:
`-DOPENMETA_WHEEL_NO_BUILD_ISOLATION=ON`.

## Interop Surfaces

Interop surfaces are split deliberately:

- export-only naming/traversal surface:
  `openmeta/interop_export.h` with the shared export naming styles. This is
  the intended base for host-owned metadata mapping layers.
- export-only adapter:
  `openmeta/ocio_adapter.h` for deterministic OCIO-style metadata trees.
- host-apply adapter:
  `openmeta/exr_adapter.h` for EXR-native per-part attribute export.
- direct bridge:
  `openmeta/dng_sdk_adapter.h` for applications that already use Adobe DNG
  SDK objects/files.
- narrow translator:
  `openmeta/libraw_adapter.h` for explicit orientation mapping into LibRaw's
  flip convention.

Current Python binding entry points:

- `Document.export_names(style=..., include_makernotes=...)`
- `Document.ocio_metadata_tree(...)`
- `Document.unsafe_ocio_metadata_tree(...)`
- `Document.dump_xmp_sidecar(format=...)`
- `Document.phaseone_raw_geometry()` and
  `Document.phaseone_raw_processing()` for normalized Phase One/Leaf RAW
  source metadata queries.
- `Document.vendor_raw_processing(family)` for
  Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/
  Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/Reconyx/HP/JVC/
  GE/Motorola/Nintendo/Microsoft grouped RAW/source-processing field
  summaries.

Current C++ interop entry points:

- `openmeta/interop_export.h`:
  - `visit_metadata(...)`
  - use the shared naming styles when a host-owned metadata mapping layer
    needs deterministic exported names
- `openmeta/exr_adapter.h`:
  - `build_exr_attribute_batch(...)`
  - `build_exr_attribute_part_spans(...)`
  - `build_exr_attribute_part_views(...)`
  - `replay_exr_attribute_batch(...)`
  - the batch carries:
    `part_index`, `name`, `type_name`, `value` bytes, and `is_opaque`
- `openmeta/ocio_adapter.h`:
  - safe API: `build_ocio_metadata_tree_safe(..., InteropSafetyError*)`
  - unsafe API: `build_ocio_metadata_tree(...)`
  - `build_ocio_metadata_tree(..., const OcioAdapterRequest&)`
  - `build_ocio_metadata_tree(..., const OcioAdapterOptions&)`

Python interop behavior:
- `Document.export_names(style=ExportNameStyle.FlatHost, ...)` exposes the
  stable v1 flat-host naming contract used by host-side metadata mapping
  layers. See [flat_host_mapping.md](flat_host_mapping.md).
- `Document.ocio_metadata_tree(...)` is safe-by-default and raises on unsafe
  raw byte payloads; use `Document.unsafe_ocio_metadata_tree(...)` for
  legacy/raw fallback output.

Current C++ sidecar entry points:

- `openmeta/xmp_dump.h`:
  - `dump_xmp_sidecar(..., const XmpSidecarRequest&)` (stable flat request API)
  - `dump_xmp_sidecar(..., const XmpSidecarOptions&)` (advanced/legacy shape)

Draft C++ transfer entry points (prepare/emit scaffold):

- `openmeta/metadata_transfer.h`:
  - `PreparedTransferBundle` (target-ready payload container)
  - backend emitter contracts:
    - `JpegTransferEmitter`
    - `TiffTransferEmitter`
    - `JxlTransferEmitter`
    - `WebpTransferEmitter`
    - `ExrTransferEmitter`
  - `prepare_metadata_for_target(..., PreparedTransferBundle*)` currently
    prepares JPEG/TIFF transfer blocks plus the current bounded JXL/WebP/BMFF
    transfer set: EXIF APP1 (JPEG) / JXL `Exif` / WebP `EXIF` / BMFF EXIF
    item, XMP (JPEG APP1 / TIFF tag 700 / JXL `xml ` box / WebP `XMP `
    chunk / BMFF XMP item), ICC (JPEG APP2 / TIFF tag 34675 / JXL encoder ICC
    profile / WebP `ICCP` chunk), IPTC (JPEG APP13 / TIFF tag 33723 or
    projected into JXL/WebP/BMFF XMP), bounded JUMBF/C2PA routes, with
    explicit warnings for unsupported/skipped entries.
  - `emit_prepared_bundle_jpeg(...)` is implemented for route-based JPEG marker
    emission (`jpeg:appN...`, `jpeg:com`).
  - `emit_prepared_bundle_tiff(...)` is implemented for route-based TIFF tag
    emission (`tiff:ifd-exif-app1`, `tiff:tag-700-xmp`, `tiff:tag-34675-icc`,
    `tiff:tag-33723-iptc`) and commit hook.
  - Current CLI TIFF rewrite path supports classic TIFF (little- and
    big-endian) for ExifIFD materialization (`tiff:ifd-exif-app1`).
  - `compile_prepared_bundle_jpeg(...)` + `emit_prepared_bundle_jpeg_compiled(...)`
    provide route-compile + reusable emit plan for high-throughput
    "prepare once, emit many" use.
  - `compile_prepared_bundle_tiff(...)` + `emit_prepared_bundle_tiff_compiled(...)`
    provide the same reusable route-compile emit plan for TIFF tag emission.
  - `apply_time_patches(...)` applies fixed-width in-place updates over
    `bundle.time_patch_map` (for example EXIF `DateTime*`, `SubSec*`,
    `OffsetTime*`, GPS date/time slots) without full re-prepare.
  - TIFF edit path mirrors JPEG edit path:
    - `plan_prepared_bundle_tiff_edit(...)`
    - `apply_prepared_bundle_tiff_edit(...)`
    (classic TIFF rewrite for prepared EXIF/XMP/ICC/IPTC updates).
  - Writer/sink edit path is available for both targets:
    - `TransferByteWriter`
    - `SpanTransferByteWriter`
    - `PreparedTransferExecutionPlan`
    - `TimePatchView`
    - `write_prepared_bundle_jpeg(...)`
    - `write_prepared_bundle_jpeg_compiled(...)`
    - `write_prepared_bundle_jpeg_edit(...)`
    - `write_prepared_bundle_tiff_edit(...)`
    - `apply_time_patches_view(...)`
    - `compile_prepared_transfer_execution(...)`
    - `execute_prepared_transfer_compiled(...)`
    - `write_prepared_transfer_compiled(...)`
    - `emit_prepared_transfer_compiled(..., JpegTransferEmitter&)`
    - `emit_prepared_transfer_compiled(..., TiffTransferEmitter&)`
    - `ExecutePreparedTransferOptions::emit_output_writer`
    - `ExecutePreparedTransferOptions::edit_output_writer`
    JPEG can stream either metadata-only emit bytes or edited output directly.
    TIFF edit output streams original input plus a planned metadata tail,
    avoiding a temporary full-file rewrite buffer.
  - `prepare_metadata_for_target_file(...)` provides the file-level
    `read/decode -> prepare bundle` step.
  - `execute_prepared_transfer(...)` runs the shared
    `time_patch -> compile -> emit -> optional edit` flow on an already
    prepared bundle.
  - `compile_prepared_transfer_execution(...)` compiles a reusable execution
    plan that stores target-specific route mapping plus emit policy.
  - `build_prepared_transfer_adapter_view(...)` flattens the same compiled
    route mapping into one target-neutral operation list for
    JPEG/TIFF/JXL/WebP/BMFF host integrations.
  - `emit_prepared_transfer_adapter_view(...)` replays that compiled view into
    one generic host sink without route parsing.
  - `apply_time_patches_view(...)` accepts non-owning patch spans for
    per-frame patching without owned update buffers.
  - `execute_prepared_transfer_compiled(...)` runs the same shared
    `time_patch -> emit -> optional edit` flow using a precompiled execution
    plan.
  - `write_prepared_transfer_compiled(...)` is the narrow encoder-integration
    helper for `prepare once -> compile once -> patch -> write` workflows.
  - `SpanTransferByteWriter` is the fixed-buffer adapter for encoder paths that
    want preallocated output memory and deterministic overflow reporting before
    any JPEG marker bytes are written.
  - `PreparedTransferPackagePlan` is the shared final-output packaging layer
    for current JPEG/TIFF rewrite paths plus direct JPEG/JXL/WebP/BMFF emit
    packaging.
    - `TransferPackageChunkKind::SourceRange` copies bytes from the original
      input stream.
    - `TransferPackageChunkKind::PreparedTransferBlock` serializes one
      prepared block directly for JPEG, JXL, WebP, PNG, JP2, or BMFF targets.
      BMFF direct packages carry stable item/property payload bytes; the file
      edit path is what wraps those payloads into a bounded top-level `meta`
      graph.
    - `TransferPackageChunkKind::PreparedJpegSegment` injects one prepared
      JPEG marker segment from the bundle.
    - `TransferPackageChunkKind::InlineBytes` carries deterministic generated
      bytes such as the patched TIFF IFD0 offset or appended TIFF tail.
  - `PreparedTransferPackageBatch` is the owned replay form of that package
    layer. It materializes each package chunk into stable bytes so host code
    can cache or hand off the final metadata package without retaining the
    original input stream or prepared bundle storage.
  - `serialize_prepared_transfer_package_batch(...)` and
    `deserialize_prepared_transfer_package_batch(...)` persist that owned
    batch for cross-process or cross-layer replay.
  - `collect_prepared_transfer_payload_views(...)` and
    `build_prepared_transfer_payload_batch(...)` provide the matching
    target-neutral semantic surface one level earlier, directly over prepared
    bundles.
  - `serialize_prepared_transfer_payload_batch(...)` and
    `deserialize_prepared_transfer_payload_batch(...)` persist that semantic
    payload batch when a host wants cross-process handoff before final package
    materialization.
  - `collect_prepared_transfer_package_views(...)` is the target-neutral
    semantic view above that persisted batch. It exposes semantic package
    chunks (`Exif`, `Xmp`, `Icc`, `Iptc`, `Jumbf`, `C2pa`, or `Unknown`)
    without pushing route parsing into host adapters.
  - `replay_prepared_transfer_package_batch(...)` is the matching target-neutral
    callback replay path over the same persisted batch.
  - OpenMeta no longer ships an in-library host-specific payload/package
    bridge above the target-neutral package and adapter surfaces.
  - `PreparedTransferAdapterView` is the parallel adapter-facing surface for
    host integrations that want explicit per-block operations without route
    parsing.
  - `build_exr_attribute_batch(...)`,
    `build_exr_attribute_part_spans(...)`,
    `build_exr_attribute_part_views(...)`, and
    `replay_exr_attribute_batch(...)` are the EXR-native bridge for
    OpenEXR header-attribute workflows. They stay outside the
    `PreparedTransferBundle` path because EXR metadata is attribute-native,
    not block-native.
  - `build_prepared_transfer_emit_package(...)`,
    `build_prepared_transfer_adapter_view(...)`,
      `emit_prepared_transfer_adapter_view(...)`,
      `build_prepared_bundle_jpeg_package(...)`,
      `build_prepared_bundle_tiff_package(...)`, and
      `write_prepared_transfer_package(...)` expose that shared contract.
  - `emit_prepared_transfer_compiled(..., TiffTransferEmitter&)` is the
    intended TIFF hot path; TIFF does not expose a metadata-only byte-writer
    emit contract.
  - `execute_prepared_transfer_file(...)` wraps the full
    `read/decode -> prepare -> execute` flow and is now the main thin-wrapper
    entry point for CLI/Python tooling.

Python transfer entry point:

- `openmeta.transfer_probe(...)` (safe):
  - calls the same file-level transfer execution API as the CLI,
    returning read/prepare/compile/emit summaries and prepared block
    routes/sizes;
  - supports `time_patches={Field: "Value" | b"..."}`
    with shared C++ patch logic inside
    `execute_prepared_transfer(...)`;
  - exposes `time_patch_*` summary fields
    (`time_patch_status_name`, `time_patch_patched_slots`, ...);
  - if `include_payloads=True`, returns
    `overall_status=unsafe_data` with `error_code=unsafe_payloads_forbidden`.
- `openmeta.unsafe_transfer_probe(...)`:
  - same probe contract, but allows `include_payloads=True` and returns raw
    payload bytes (`bytes`) in `blocks[i].payload`.
  - intended for explicit raw/unsafe workflows only.
- Snapshot/fileless Python helpers:
  - `openmeta.read_transfer_source_snapshot_file(...)` and
    `openmeta.read_transfer_source_snapshot_bytes(...)` expose the reusable
    decoded-source contract directly.
  - `Document.build_transfer_source_snapshot()` and
    `openmeta.build_transfer_source_snapshot(document)` mirror the C++
    `MetaStore -> TransferSourceSnapshot` builder.
  - `openmeta.transfer_snapshot_probe(...)` /
    `openmeta.transfer_snapshot_file(...)` expose the core snapshot-based
    execute/persist path, including host-owned `target_bytes`.
  - `openmeta.unsafe_transfer_snapshot_probe(...)` /
    `openmeta.unsafe_transfer_snapshot_file(...)` add optional edited-output
    bytes for explicit unsafe workflows.
  - the Python transfer wrappers now distinguish
    `xmp_existing_sidecar_base_path` from `xmp_sidecar_base_path`, and they
    also expose `xmp_existing_destination_embedded_path` plus
    `xmp_existing_destination_sidecar_state` for pathless host flows.
  - `openmeta.python.metatransfer` remains a thin command-line wrapper: its
    `--xmp-writeback`, `--xmp-destination-embedded`,
    `--xmp-destination-sidecar`, `--output`, and `--force` flags map directly
    onto the C++ file-helper options and persistence flags. It reports the
    sidecar and cleanup paths returned by the C++ result instead of deriving a
    separate Python-side contract.

Transfer probe contract hardening (stable machine fields):
- `overall_status`, `overall_status_name`
- `error_stage` (`none|api|file|prepare|emit`)
- `error_code`, `error_message`
- stage-specific stable code enums/strings:
  - file: `PrepareTransferFileCode` / `file_code_name`
  - prepare: `PrepareTransferCode` / `prepare_code_name`
  - emit: `EmitTransferCode` / `emit_code_name`

Current adapter/name-policy behavior:

- `ExportNamePolicy::ExifToolAlias` applies compatibility aliases for
  interop-name parity workflows.
- `ExportNamePolicy::Spec` preserves spec/native names.
- Shared flat-host interop naming keeps numeric unknown names (for example
  `Exif_0x....`) for parity workflows.
- When DNG context is detected (`DNGVersion` present in the same IFD), DNG
  color/CCM tags are exported with dedicated adapter namespaces:
  `dng:*` (portable) and a flat host-style variant.
- ICC entries are exported with adapter-friendly names:
  `icc:*` (portable) and a flat host-style variant, alongside canonical
  `icc:header:*` / `icc:tag:*` naming.

Adapter-focused tests (public tree):

```bash
cmake --build build-tests --target openmeta_tests
./build-tests/openmeta_tests --gtest_filter='InteropExport.*:OcioAdapter.*:ExrAdapter.*'
./build-tests/openmeta_tests --gtest_filter='ExrAdapter.*'
./build-tests/openmeta_tests --gtest_filter='CrwCiffDecode.*'
```

Notes:
- `InteropExport` tests cover alias/spec behavior and the flat host-style
  naming contract.
- `ExrAdapter` tests cover EXR batch export and replay behavior.
- `CrwCiffDecode` tests cover CRW/CIFF derived EXIF mapping for legacy Canon RAW.

## Doxygen (Optional)

Requirements:
- `doxygen` (optional: `graphviz`)

Generate API docs:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENMETA_BUILD_DOCS=ON
cmake --build build --target openmeta_docs
```

## Sphinx Docs (Optional)

Requirements:
- `doxygen`
- Python packages listed in `docs/requirements.txt` (Sphinx + Breathe; `furo` is optional)

Install the Python deps into your active environment (example with `uv`):
```bash
uv pip install -r docs/requirements.txt
```

Build:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENMETA_BUILD_SPHINX_DOCS=ON
cmake --build build --target openmeta_docs_sphinx
```

Install:
```bash
cmake --install build --prefix /tmp/openmeta-install
ls /tmp/openmeta-install/share/doc/OpenMeta/html/index.html
```

The exported CMake package is installed under
`${CMAKE_INSTALL_LIBDIR}/cmake/OpenMeta`. On Unix this may resolve to a
multiarch path such as `lib/x86_64-linux-gnu/cmake/OpenMeta` when the install
prefix is `/usr`.

When both `OPENMETA_BUILD_SPHINX_DOCS=ON` and `OPENMETA_BUILD_DOCS=ON`, the
Doxygen HTML output is installed under `share/doc/OpenMeta/doxygen/html`.
