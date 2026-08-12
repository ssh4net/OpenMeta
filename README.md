<img width="500" height="79" alt="OpenMeta" src="docs/images/OpenMeta_Logo.png" />

[![CI](https://github.com/ssh4net/OpenMeta/actions/workflows/ci.yml/badge.svg)](https://github.com/ssh4net/OpenMeta/actions?query=workflow%3ACI)
[![docs-pages](https://github.com/ssh4net/OpenMeta/actions/workflows/docs-pages.yml/badge.svg)](https://github.com/ssh4net/OpenMeta/actions?query=workflow%3Adocs-pages)

OpenMeta is a metadata processing library for image files. It does not decode,
decompress, demosaic, or render image pixels.

The current focus is format-agnostic metadata reads: find metadata blocks in
common containers, decode them into a normalized in-memory model, and expose
bounded transfer/edit building blocks for export workflows.

## What OpenMeta Does

- Scan containers to locate metadata blocks in JPEG, PNG, WebP, GIF, TIFF/DNG,
  JP2, JXL, ISO-BMFF (HEIF/AVIF/CR3), CRW/CIFF, RAF, and X3F files.
- Reassemble chunked payloads and optionally decompress supported carriers.
- Decode metadata into a normalized `MetaStore`.
- Create a fresh finalized metadata store from bounded logical portable fields.
- Edit logical portable fields transactionally without mutating the source
  store.
- Export sidecars and previews.
- Prepare, compile, emit, and edit metadata transfers for bounded target
  families.

## Camera RAW Metadata Scope

Camera RAW names do not all identify separate OpenMeta scanners. Many camera
formats share a TIFF/EXIF metadata carrier, while CR3, CRW, RAF, and X3F need
dedicated container handling.

| Read lane | Camera RAW families | Current metadata scope |
| --- | --- | --- |
| Shared TIFF/EXIF carrier | DNG, CR2, NEF/NRW, ARW, RW2, ORF, PEF, SR2/SRF, and other TIFF-based RAW | Strong common TIFF/EXIF decoding; MakerNote and vendor-private interpretation varies by family and model. Legacy SR2/SRF coverage includes the shared metadata carrier, not every native private structure. |
| Dedicated ISO-BMFF lane | CR3 | Bounded EXIF/XMP/ICC, Canon maker blocks, and metadata item/property graph interpretation; not complete CR3 private-structure conformance. |
| Dedicated native lanes | CRW/CIFF, RAF, X3F | Partial native container and private metadata interpretation with raw preservation where the structure is not safely understood. |

Vendor metadata for additional camera families is interpreted where layouts
are stable and testable; that does not imply that every model-specific private
table is named. In this documentation, camera RAW *read support* means metadata
carrier discovery plus the documented structured interpretation. It does not
mean RAW pixel decoding, raw-sample decompression, demosaicing, complete vendor
private-data decoding, or native camera RAW writing. Apart from the bounded DNG
and CR3 target contracts described below, read support does not imply write
support.

For vendor-by-vendor depth and known gaps, see
[docs/raw_read_parity_plan.md](docs/raw_read_parity_plan.md).

## Metadata Families

OpenMeta currently covers these major families:

- EXIF, including pointer IFDs and broad MakerNote support.
- Legacy Canon CRW/CIFF bridge with bounded native CIFF naming and projection.
- XMP as RDF/XML properties.
- ICC profile header and tag table.
- Photoshop IRB, with raw preservation plus a bounded interpreted subset.
- IPTC-IIM datasets.
- JPEG comments, GIF comments, and PNG text chunks.
- ISO-BMFF derived fields for brand, primary-item, item-semantic, relation,
  whole-scene graph policy, and auxiliary semantics.
- JUMBF / C2PA draft structural and semantic projection.
- EXR header attributes.

**C2PA security warning:** the optional verification scaffold is diagnostic
only and must not be used as an asset-authenticity or trust gate. It does not
currently establish the manifest's hard binding to the complete asset, and its
certificate-chain result is not reliably bound to the signature key. See the
[2026-08-12 security review](https://github.com/ssh4net/OpenMeta/blob/main/code_review_20260812.md).

For the detailed support matrix, see
[docs/metadata_support.md](docs/metadata_support.md).

## Start Here

If you are new to the project, start with
[docs/quick_start.md](docs/quick_start.md).

That guide covers the shortest useful paths for:

- reading and querying metadata in C++
- building and editing `MetaStore`
- copying metadata into an existing JPEG, TIFF, or DNG target
- building EXR and host-API metadata outputs
- using the optional Adobe DNG SDK bridge

If you already own the encoder, SDK objects, or output container, follow
[docs/host_integration.md](docs/host_integration.md) next.

## Documentation

- https://ssh4net.github.io/OpenMeta/: published documentation site
- [docs/quick_start.md](docs/quick_start.md): shortest adoption path
- [docs/host_integration.md](docs/host_integration.md): C++ host and encoder
  integration patterns
- [docs/metadata_support.md](docs/metadata_support.md): metadata support matrix
- [docs/metadata_transfer_plan.md](docs/metadata_transfer_plan.md): transfer
  status and roadmap
- [docs/fuzzy_search.md](docs/fuzzy_search.md): optional ranked metadata-name
  search contract, quality gates, and benchmark
- [docs/creation.md](docs/creation.md): bounded fresh metadata construction,
  field mapping, validation, and Python use
- [docs/editing.md](docs/editing.md): transactional logical add, set, remove,
  provenance, conflict, and Python behavior
- [docs/shared_library.md](docs/shared_library.md): shared-library ABI,
  toolchain, runtime, and installed-consumer contract
- [docs/doxygen.md](docs/doxygen.md): API reference
- [SECURITY.md](https://github.com/ssh4net/OpenMeta/blob/main/SECURITY.md):
  security model and reporting
- [NOTICE.md](https://github.com/ssh4net/OpenMeta/blob/main/NOTICE.md): notices
  and third-party dependency information

## Naming Model

EXIF and MakerNote display names have two layers:

- Canonical names from `exif_tag_name(...)`
- ExifTool-compatible display names from
  `exif_entry_name(..., ExifTagNamePolicy::ExifToolCompat)`

That split lets OpenMeta keep stable internal naming while still matching
common external tooling where compatibility matters.

## Transfer and Edit

OpenMeta now has a real transfer core built around:

- `prepare_metadata_for_target(...)`
- `compile_prepared_transfer_execution(...)`
- `execute_prepared_transfer(...)`
- `execute_prepared_transfer_file(...)`

Current target status is described in terms of bounded metadata operations,
not complete arbitrary container or image writers:

| Target | Current contract |
| --- | --- |
| JPEG | Strongest current managed-metadata edit path |
| TIFF | Strongest current managed-metadata edit path, with documented nested-IFD bounds |
| DNG | Strong TIFF-backed metadata target; not a general DNG image writer |
| PNG | Bounded managed-metadata edit path |
| WebP | Bounded managed-metadata edit path |
| JP2 | Bounded managed-metadata edit path |
| JXL | Bounded managed-metadata edit path |
| HEIF / AVIF / CR3 | Bounded ISO-BMFF metadata-graph edit path |
| EXR | Bounded host attribute emission; no full EXR file rewrite |

In practice:
- JPEG, TIFF, and DNG are the strongest transfer targets today.
- TIFF edit support now covers classic TIFF, BigTIFF, bounded preview-page
  chain rewrite (`ifd1`, `ifd2`, and preserved downstream tails), and bounded
  SubIFD rewrite with preserved downstream auxiliary tails and preserved
  trailing existing children when only the front subset is replaced. Replaced
  `ExifIFD` blocks can also preserve an existing target `InteropIFD` when the
  source does not supply its own interop child.
- DNG is now a dedicated public transfer target layered on the TIFF backend.
  The current bounded DNG contract covers read-backed file-helper roundtrips,
  `DNGVersion` preservation, minimal `DNGVersion` synthesis when the source
  metadata lacks it, bounded preview-page chain rewrite/merge,
  bounded raw-image `SubIFD` rewrite/merge, preserved downstream page/aux
  tails, preserved trailing existing auxiliary children, and bounded
  `ExifIFD -> InteropIFD` preservation. When a non-DNG source is merged into
  an existing DNG target, the target's core DNG tags and preview/raw
  structure are preserved under that same bounded contract. The public DNG
  transfer contract is now explicit:
  - `ExistingTarget`
  - `TemplateTarget`
  - `MinimalFreshScaffold`
  Existing/template modes require a target path in the file-helper flow;
  minimal fresh scaffold keeps the metadata-only DNG prepare path available
  without claiming a full standalone DNG writer.
- When built with `OPENMETA_WITH_DNG_SDK_ADAPTER=ON` and a `dng_sdk`
  package is available, OpenMeta also exposes
  [dng_sdk_adapter.h](src/include/openmeta/dng_sdk_adapter.h) as an optional
  host bridge for applying prepared DNG-target metadata onto Adobe DNG SDK
  `dng_negative` / `dng_stream` objects. That bridge now includes:
  - direct prepared-bundle apply/update entry points for SDK object owners
  - a public file-helper for `source file -> existing DNG file` in-place
    metadata update
  - a matching thin Python binding over that file-helper
  - a thin CLI helper via `metatransfer --update-dng-sdk-file <target.dng>`
  Core `Dng` transfer support does not depend on that SDK. The OpenMeta
  build must use a C++ runtime/standard library compatible with the
  discovered `dng_sdk` package. Public automated CI intentionally excludes
  this SDK-backed lane because Adobe DNG SDK licensing and redistribution
  terms are not part of the public CI dependency story; SDK-backed coverage is
  treated as maintainer or release validation.
- PNG, WebP, JP2, JXL, bounded BMFF, and EXR all have implemented, bounded
  transfer entry points. BMFF file edits can replace OpenMeta-authored
  metadata-only `meta` boxes and can merge, replace, or strip bounded
  Exif/XMP/JUMBF/C2PA metadata items plus bounded ICC `colr/prof` properties
  in a parseable foreign top-level `meta` graph. Arbitrary BMFF
  scene/property-graph rewrite remains out of scope for the bounded writer
  path.
- EXR is still narrower than the container-edit targets: it emits safe string
  header attributes through the transfer core, can materialize a prepared
  `ExrAdapterBatch` for host exporters, and Python can inspect that prepared
  EXR attribute batch through the direct `build_exr_attribute_batch_from_file`
  binding or the helper-layer `openmeta.python.get_exr_attribute_batch(...)`,
  but OpenMeta does not rewrite full EXR files yet.
- Writer-side sync behavior is now partially explicit instead of implicit:
  generated XMP can independently keep or suppress EXIF-derived and
  IPTC-derived projection during transfer preparation.
- Generated portable XMP also has an explicit conflict policy for existing
  decoded XMP versus generated EXIF/IPTC mappings:
  current behavior, `existing_wins`, or `generated_wins`.
- Generated portable XMP now also has an explicit existing-namespace policy:
  keep only OpenMeta's known portable namespaces, or preserve safe custom
  existing namespaces with deterministic generated prefixes.
- Transfer preparation can also fold an existing sibling `.xmp` sidecar from
  the destination path into generated portable XMP when that bounded mode is
  requested, with explicit `sidecar_wins` or `source_wins` precedence against
  source-embedded existing XMP.
- Transfer preparation and file-helper execution can also fold existing
  embedded XMP from the destination file into generated portable XMP when
  that bounded mode is requested, with explicit `destination_wins` or
  `source_wins` precedence against source-embedded existing XMP.
- File-helper execution, `metatransfer`, and the Python transfer wrapper now
  share a bounded XMP carrier choice:
  embedded XMP only, sidecar-only writeback to a sibling `.xmp`, or dual
  embedded-plus-sidecar writeback when a generated XMP packet exists for the
  prepared transfer.
- `metatransfer` and the Python transfer wrapper also expose the bounded
  destination-embedded merge controls directly instead of hiding them behind
  lower-level bindings.
- Sidecar-only writeback also has an explicit destination embedded-XMP policy:
  preserve existing embedded XMP by default, or strip it for
  `jpeg`, `tiff`, `png`, `webp`, `jp2`, and `jxl`.
- Embedded-only writeback can also strip an existing sibling `.xmp`
  destination sidecar explicitly, so exports can move back to embedded-only
  XMP without leaving stale sidecar state behind.
- C++ hosts now also have a bounded persistence helper for file-helper
  results, so edited output bytes, generated sidecars, and stale-sidecar
  cleanup can be applied without copying wrapper logic.
- Python hosts also have matching `transfer_file(...)` and
  `unsafe_transfer_file(...)` bindings, and the public Python transfer wrapper
  now uses that same core-backed persistence path for real writes.
- Prepared bundles record resolved policy decisions for MakerNote, JUMBF,
  C2PA, EXIF-to-XMP projection, and IPTC-to-XMP projection.
- This is still not a full MWG-style sync engine. OpenMeta does not yet try to
  solve all EXIF/IPTC/XMP conflict resolution or full canonical writeback
  policy.

For transfer details, see
[docs/metadata_transfer_plan.md](docs/metadata_transfer_plan.md).

## Tools

OpenMeta ships a small set of CLI tools:

| Tool | Purpose |
| --- | --- |
| `metaread` | Human-readable metadata dump |
| `metavalidate` | Metadata validation and issue reporting |
| `metadump` | Sidecar and preview dump tool |
| `metatransfer` | Transfer/edit smoke tool over the core transfer APIs |
| `thumdump` | Preview extractor |

The optional Python bindings remain a thin layer over the same C++ read, query,
fuzzy-search, creation, editing, and transfer core. Available Python features
follow the capabilities compiled into the C++ library.

## Layout

- `src/include/openmeta/`: public headers
- `src/openmeta/`: library implementation
- `src/tools/`: CLI tools
- `src/python/`: Python bindings and helper scripts
- `tests/`: unit tests and fuzz targets
- `docs/`: design notes and developer documentation

## Status

Read-path coverage is broad and regression-gated. Write/edit support is real
for the main transfer targets, but parts of that API surface are still draft
and may change as the transfer contract stabilizes.

The following values are roadmap estimates for the currently tracked public
scope. They are not percentages of every vendor tag or file in existence, full
format conformance, direct competitor parity, or overall product completion.

| Milestone | Status |
| --- | --- |
| Decoding/read milestone for the current target scope | About `98-100%` |
| Transfer / export milestone | About `80-85%` |

Creation, Editing, Translation, Writing, Adapters, and Utilities have separate
readiness levels. See [docs/development.md](docs/development.md) for the full
stage table.

Current baseline-gated snapshot on tracked corpora:
- HEIC/HEIF, CR3, and mixed RAW EXIF tag-id compare gates are passing.
- EXR header metadata compare is passing for name/type/value-class checks.
- Portable and lossless sidecar export paths are covered by baseline and smoke
  gates.
- MakerNote decode is baseline-gated with broad vendor support; unknown tags
  are preserved losslessly when no structured mapping exists.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Useful options:
- `-DOPENMETA_BUILD_STATIC=ON|OFF` and
  `-DOPENMETA_BUILD_SHARED=ON|OFF` to select library variants
- `-DOPENMETA_BUILD_TOOLS=ON|OFF`
- `-DOPENMETA_BUILD_TESTS=ON` for GoogleTest-based unit tests
- `-DOPENMETA_BUILD_FUZZERS=ON` for Clang + libFuzzer targets
- `-DOPENMETA_BUILD_PYTHON=ON` for the optional nanobind bindings
- `-DOPENMETA_BUILD_WHEEL=ON` to add the isolated Python wheel target
- `-DOPENMETA_WITH_ZLIB=ON`, `-DOPENMETA_WITH_BROTLI=ON`, and
  `-DOPENMETA_WITH_EXPAT=ON` to request compressed metadata and structured XMP
  decoding. These capabilities are enabled only when CMake discovers the
  matching dependency.
- `-DOPENMETA_ENABLE_RAPIDFUZZ=ON` for optional RapidFuzz-backed fuzzy search
- `-DOPENMETA_ENABLE_C2PA_VERIFY=ON` for the draft diagnostic verification
  scaffold. Do not use its current result as an authenticity or trust gate.
- `-DOPENMETA_USE_LIBCXX=ON` when linking against dependencies built with
  `libc++`
- `-DOPENMETA_TEST_RUNTIME_LIBRARY_PATH=/path/to/runtime-libs` when CTest
  launches external tools that need a non-default C++ runtime lookup path
- `-DOPENMETA_WITH_DNG_SDK_ADAPTER=ON` to enable the optional Adobe DNG SDK
  bridge (requires a discoverable `dng_sdk` package; intentionally excluded
  from public GitHub Actions CI)
- `-DOPENMETA_BUILD_DOCS=ON` for Doxygen HTML docs
- `-DOPENMETA_BUILD_SPHINX_DOCS=ON` for Sphinx + Breathe HTML docs

Developer notes live in [docs/development.md](docs/development.md).

## Quick Usage

The shortest CLI path is:

```bash
./build/metaread file.jpg
./build/metatransfer --source-meta source.jpg --target-jpeg rendered.jpg --output rendered_with_meta.jpg --force
```

For C++, host-API, and Python examples, use
[docs/quick_start.md](docs/quick_start.md).
