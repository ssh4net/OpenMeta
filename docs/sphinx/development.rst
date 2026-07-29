Developer Notes
===============

Repository layout (public):

- ``src/include/openmeta/``: public headers
- ``src/openmeta/``: implementation
- ``src/tools/``: CLI tools
- ``src/python/``: Python bindings and helper scripts
- ``tests/``: unit tests and fuzz targets

OpenMeta structure
------------------

See :doc:`interpretation_status` for the semantic interpretation matrix.

OpenMeta's public architecture is organized around a small set of user-facing
capabilities. Internally some of these split into more stages, but the public
model should stay compact:

.. list-table::
   :header-rows: 1
   :widths: 16 64 20

   * - Area
     - Purpose
     - Readiness
   * - Decoding
     - Find metadata carriers and decode EXIF, XMP, IPTC, ICC, Photoshop IRB,
       JUMBF/C2PA, EXR, and related blocks into ``MetaStore`` entries.
     - High, about 98-100% for the current target scope.
   * - Interpretation
     - Normalize names and values, group entries by meaning, and classify
       source-bound data such as RAW crop, exposure adjustment,
       color/profile/source-color-transform evidence, RAW curves/linearity
       metadata with descriptor-backed compressed-storage applicability,
       lens-correction, sensor, BMFF brand/item-property
       associations and rollups including ``avio`` AVIF brands, item groups,
       item semantic counts, whole-scene policy hints including multi-image
       policy text, graph-component summaries with per-component roles,
       member item IDs, semantic composition, direction-correct relation
       endpoint roles and named item-id aliases, semantic item-group roles,
       typed relation counts, and bounded ``grid``/``iovl``/``iden``
       construction semantics with recursive item-offset descriptors and
       graph-cycle/source validation, bounded complete ``tili`` configuration/
       reference/offset-table interpretation, ``container_graph``
       concepts, primary item properties,
       primary metadata-carrier flags, primary sidecar and scene summaries,
       and display-transform summaries, JUMBF labels, Photoshop
       IRB embedded carriers plus
       fixed-layout, XML/text, working-path and numbered clipping-path
       records, byte-count, descriptor-header,
       scalar/class/alias/enum/reference, and bounded nested list/object
       summaries,
       EXIF 3.1 correction/noise labels, version/firmware-style value
       formatting for selected EXIF/Nikon/Olympus/native RAF contexts,
       selected Sony ILCE-7RM6 correction-offset routing, Fujifilm flash
       white-balance naming, Apple/FLIR/JVC/GE/Reconyx/Microsoft/Nintendo/
       Sanyo scalar MakerNote labels, current Canon RF/Nikon Z lens labels,
       an ambiguous Pentax Sigma/Samsung/Tokina lens-family label,
       EXIF/XMP GPS timestamp composites, EXIF ``OffsetTime*``/
       ``SubSecTime*`` date composition with normalized-instant conflict
       checks, separate camera/destination/shown/created GPS roles with
       scope-aware structured-location conflicts,
       language-aware descriptive roles, legacy editorial duplicate
       reconciliation, source-bound IPTC technical-image/audio/preview
       records, accessibility/taxonomy/registry/image-region/
       document-identity/lineage/history semantics,
       structured creator-contact/event/person/organization/product/artwork/
       rights/license/release/end-user/image-creator/image-supplier/image-asset/
       controlled-vocabulary/registry/image-region/image-region-boundary/
       resource-reference/resource-event/manifest-item/version/
       technical-image/audio-asset/preview-asset/pantry records,
       explicit image-region shape and coordinate-unit contracts,
       and independent privacy/policy sensitivity,
       computational, thermal, stitch/panorama capture state, and
       vendor-private fields.
     - High, measured about 99.85% for declared semantic targets.
   * - Query
     - Find entries by name, fuzzy term, or semantic group, then expose
       normalized query candidates, structured interpretation records, and
       bounded cross-family concept resolutions, transfer hints, sensitivity,
       and conflict
       flags for crop/border/active-area, exposure/gain,
       color/WB/profile/source-color-transform, orientation, date/time, GPS,
       descriptive fields including contact/event/person/organization/product/
       artwork/rights/license/release, editorial, accessibility, taxonomy,
       registry, image-region, document-identity, document-lineage, and
       document-history, technical-image, audio, and preview semantics,
       lens-correction, computational/thermal/stitch, and RAW/source-processing
       fields plus BMFF derived-image construction and tiled-image configuration
       evidence across standard and vendor metadata.
     - High, measured about 99.77% for declared query targets.
   * - Creation
     - Build fresh metadata entries from host-provided values.
     - Medium, about 55-65%.
   * - Editing
     - Modify existing logical metadata entries while preserving valid
       surrounding structure.
     - Medium, about 60-70%.
   * - Transfer
     - Move metadata between files using explicit compatible-file or
       rendered-image safety policies.
     - Medium-high, about 80-85%.
   * - Translation
     - Project metadata between families, mainly bounded EXIF/IPTC/XMP portable
       mappings.
     - Medium, about 60-70%.
   * - Writing
     - Serialize metadata and write or rewrite it into target containers.
     - Medium, about 65-75%.
   * - Adapters
     - Thin integration layers for host APIs or format-specific ecosystems such
       as EXR, DNG SDK, LibRaw orientation mapping, and flat host exports.
     - Medium, about 60-70%.
   * - Utilities
     - Small standalone helpers such as capability queries, compatibility
       dumps, safety audits, tag-name lookup, version-value formatting, and
       orientation conversion.
     - Medium, about 65-75%.

The measured interpretation and query values use explicit target sets.
Unknown private numeric IDs and intentionally opaque payloads are tracked
separately rather than counted as failed semantics. The remaining measured
tail is primarily malformed, undefined, or incomplete metadata. Decoding
retains a ``98-100%`` range because not every declared container lane has an
independent conformance sample set, even though tracked inputs have explicit
read outcomes.

Query results should expose both inspection-level matches and interpreted
candidates. A crop query, for example, may match separate
``DefaultCropOrigin`` and ``DefaultCropSize`` tags, an ``ActiveArea`` rectangle,
vendor margin fields, or a raw integer array. OpenMeta should return the source
entries, confidence, value shape, match provenance, and any normalized
interpretation rather than hiding ambiguity behind a single value.

The first experimental C++ query surface is ``openmeta/metadata_query.h``.
It returns both raw matches and normalized candidates for crop/active-area,
exposure/gain, white balance, color/profile, lens correction, orientation,
descriptive, and RAW-processing queries.
Crop queries include DNG crop tags, ``ActiveArea``, Phase One/Leaf raw
geometry, Fujifilm RAF raw crop/zoom rectangles, Canon aspect/crop metadata,
Nikon Capture crop bounds, Sony panorama crop margins, and fuzzy
crop/border-style XMP property paths. The non-crop queries expose per-entry
value candidates and reuse standard tag names, selected DNG tags, fuzzy XMP
paths, canonical border-margin parsing, and vendor RAW-processing
classification where applicable.
They also append grouped candidates for related DNG color matrix/calibration/
reduction/forward matrix tags, DNG white-balance vector tags, and
lens-correction table groups. Color queries expose a distinct
``color_profile`` semantic for EXIF color-space evidence, ICC header/tag
entries, XMP ICC/profile/color-space fields, and PNG profile text carriers.
Vendor-classified MakerNote/RAW fields can also form per-family grouped
candidates for white balance, color, raw-storage, sensor, computational,
thermal, stitch/panorama, source-processing, raw value curve, linearity-limit,
calibration-curve, and curve-control-point records. RAW-processing queries add
conservative groups for black/white levels, linearization tables, RAW value
curves, RAW linearity limits, RAW calibration curves, RAW curve control points,
CFA/sensor layout, source geometry, raw-storage identifiers, and source-private
processing buckets.
Exposure/gain concept resolution promotes exposure time, aperture, ISO,
exposure bias, exposure program/mode, gain, and raw exposure-adjustment records
into host-visible roles, with raw exposure adjustments kept unsafe for rendered
targets. Standard EXIF exposure program/mode and gain-control values and
selected Canon/Nikon/Sony/Fujifilm/Pentax/Olympus/Panasonic/Phase One/Kodak/
Minolta/Sigma/Samsung/Ricoh/Apple/FLIR/JVC/GE/Reconyx/Microsoft/Nintendo/
Sanyo MakerNote scalar print conversions are exposed as bounded labels when a
stable enum mapping is
available.
Version/firmware-like fields use ``exif_tag_numeric_value_format(...)`` and
``exif_tag_byte_value_format(...)`` instead of enum-label lookup where the
value is a formatted payload rather than a closed numeric choice.
Current source-private aliases include camera-to-XYZ/RGB matrices, creative and
picture styles, film simulation, dynamic-range processing, optical/lens
correction, white-balance gains, and raw-development terms.
Grouped candidates use ``matrix_set``, ``vector_set``, and ``table`` value
shapes. Color matrix sets, white-balance vector sets, and lens-correction
tables are promoted only when the numeric payloads meet conservative minimum
shapes; other records stay visible as per-entry matches/candidates. When
``OPENMETA_ENABLE_RAPIDFUZZ=ON``, the same query helpers also use RapidFuzz to
score near-miss XMP/property paths; default builds keep the deterministic
substring/tag matcher only. Each raw match reports ``exact_match``,
``fuzzy_match``, and ``fuzzy_score`` so UI code can distinguish exact tag/name
matches from near-miss search hits.
Python ``Document`` and ``TransferSourceSnapshot`` mirror this as thin wrappers
returning the same match/candidate dictionary shape.

For code that wants an iterable semantic record stream instead of raw query
matches, use ``openmeta/metadata_interpretation.h``. It projects query
candidates into records with query class, semantic kind, normalized shape,
confidence, source entries, and normalized geometry/value arrays where
available.

For cross-family duplicated concepts, use ``openmeta/metadata_concepts.h``.
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
resource-reference, resource-event, manifest-item, version,
editorial-workflow, source-software, editorial-contact, technical-image,
audio-asset, preview-asset, and pantry records,
including non-equivalent IPTC taxonomy/workflow fields, content-location and
prior-envelope scopes, and editorial release/expiration date-time pairs,
independent policy
sensitivity,
canonical geometry origin/size/rect/margins,
normalized exposure values, full normalized value vectors for grouped
matrix/vector/table records, transfer hints, compatible and rendered safety
booleans, and same-role conflict flags. This is deliberately an
inspection/policy surface; host code still decides whether a conflict should
be shown, ignored, or corrected during editing/transfer.

Read-path coverage snapshot
---------------------------

- Tracked HEIC/HEIF, CR3, and mixed RAW EXIF compare gates are passing.
- EXR header metadata compare gate is passing for the documented
  name/type/value-class contract.
- MakerNote support is broad and baseline-gated; unknown tags remain lossless.

EXIF + MakerNotes (code organization)
-------------------------------------

- Core EXIF/TIFF decoding: ``src/openmeta/exif_tiff_decode.cc``
- CRW/CIFF decode + derived EXIF bridge: ``src/openmeta/crw_ciff_decode.cc``
- Vendor MakerNote decoders: ``src/openmeta/exif_makernote_*.cc``
  (Canon, Nikon, Sony, Olympus, Pentax, Casio, Panasonic, Kodak, Ricoh, Samsung, FLIR, etc.)
- Shared internal-only helpers: ``src/openmeta/exif_tiff_decode_internal.h``
  (not installed)
- Unit tests for MakerNote paths: ``tests/makernote_decode_test.cc``

Internal helper conventions (used by vendor decoders):

- ``read_classic_ifd_entry(...)`` + ``ClassicIfdEntry``: parse a single 12-byte classic TIFF IFD entry.
- ``resolve_classic_ifd_value_ref(...)`` + ``ClassicIfdValueRef``: compute the value location/size for a classic IFD entry (inline vs out-of-line), using ``MakerNoteLayout`` + ``OffsetPolicy``.
- ``MakerNoteLayout`` + ``OffsetPolicy``: makes "value offsets are relative to X" explicit for vendor formats. ``OffsetPolicy`` supports both the common unsigned base (default) and a signed base for vendors that require it (eg Canon).
- ``ExifContext``: a small, decode-time cache for frequently accessed EXIF values.
- MakerNote tag-name tables are generated from ``registry/exif/makernotes/*.jsonl`` and looked up via binary search (``exif_makernote_tag_names.cc``).

Interop adapters
----------------

- export-only naming/traversal surface:
  ``src/include/openmeta/interop_export.h``
- export-only adapter:
  ``src/include/openmeta/ocio_adapter.h``
- host-apply adapter:
  ``src/include/openmeta/exr_adapter.h``
- direct bridge:
  ``src/include/openmeta/dng_sdk_adapter.h``
- narrow translator:
  ``src/include/openmeta/libraw_adapter.h``

Notes:

- ``ExportNamePolicy::ExifToolAlias`` and ``ExportNamePolicy::Spec`` are both
  covered by interop tests and used for split-parity workflows.
- Flat host-style interop naming keeps numeric unknown names
  (``Exif_0x....``) for parity workflows.

Python binding entry points:

- ``Document.export_names(...)``
- ``Document.ocio_metadata_tree(...)``
- ``Document.unsafe_ocio_metadata_tree(...)``
- ``Document.dump_xmp_sidecar(...)`` (lossless or portable via format switch)
- ``Document.phaseone_raw_geometry()`` and
  ``Document.phaseone_raw_processing()`` for normalized Phase One/Leaf RAW
  source metadata queries.
- ``Document.vendor_raw_processing(family)`` for
  Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/
  Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/Reconyx/HP/JVC/
  GE/Motorola/Nintendo/Microsoft grouped RAW/source-processing field
  summaries.

C++ adapter entry points:

- ``visit_metadata(...)`` in ``openmeta/interop_export.h``
  is the intended base for host-owned metadata mappings
- ``build_exr_attribute_batch(...)`` in ``openmeta/exr_adapter.h``
  exports one owned EXR-native attribute batch
  (``part_index``, ``name``, ``type_name``, ``value``, ``is_opaque``)
  from ``MetaStore``
- ``build_exr_attribute_part_spans(...)`` groups that batch into contiguous
  per-part spans
- ``build_exr_attribute_part_views(...)`` exposes zero-copy grouped per-part
  views over the same batch
- ``replay_exr_attribute_batch(...)`` replays the grouped batch through
  explicit host callbacks

Python typed behavior:

- ``Document.export_names(style=ExportNameStyle.FlatHost, ...)`` exposes the
  stable v1 flat-host naming contract used by host-side metadata mappings.
  See :doc:`flat_host_mapping`.
- ``Document.ocio_metadata_tree(...)`` is safe-by-default and raises on unsafe
  raw byte payloads; use ``Document.unsafe_ocio_metadata_tree(...)`` for
  legacy/raw fallback output.
- safe API: ``build_ocio_metadata_tree_safe(..., InteropSafetyError*)``
- unsafe API: ``build_ocio_metadata_tree(...)``
- ``build_ocio_metadata_tree(..., const OcioAdapterRequest&)`` in
  ``openmeta/ocio_adapter.h`` (stable flat request API)
- ``build_ocio_metadata_tree(..., const OcioAdapterOptions&)`` (advanced/legacy shape)

C++ XMP sidecar entry points:

- ``dump_xmp_sidecar(..., const XmpSidecarRequest&)`` in
  ``openmeta/xmp_dump.h`` (stable flat request API)
- ``dump_xmp_sidecar(..., const XmpSidecarOptions&)`` (advanced/legacy shape)

Optional dependencies
---------------------

OpenMeta's core scanning and EXIF/TIFF decoding do not require third-party
libraries. Some metadata payloads are compressed or structured; these optional
dependencies let OpenMeta decode more content:

- **Expat** (``OPENMETA_WITH_EXPAT``): parses XMP RDF/XML packets (embedded
  blocks and ``.xmp`` sidecars) using a streaming parser with strict limits.
- **RapidFuzz** (``OPENMETA_ENABLE_RAPIDFUZZ``): opt-in semantic-query name
  matching for inspection/search UI. It is disabled by default; when enabled,
  CMake requires either a ``rapidfuzz::rapidfuzz`` package target or
  ``OPENMETA_RAPIDFUZZ_INCLUDE_DIR`` pointing at headers containing
  ``rapidfuzz/fuzz.hpp``.
- **zlib** (``OPENMETA_WITH_ZLIB``): inflates Deflate-compressed payloads such
  as PNG ``iCCP`` (ICC profiles) and compressed text/XMP chunks (``iTXt``,
  ``zTXt``).
- **Brotli** (``OPENMETA_WITH_BROTLI``): decompresses JPEG XL ``brob`` "compressed
  metadata" boxes so wrapped metadata payloads can be decoded.

CLI tool
--------

``metaread`` prints a human-readable dump of blocks and decoded entries
(EXIF/TIFF-IFD tags, XMP properties, IPTC-IIM datasets, ICC profile fields/tags,
and Photoshop IRB resource blocks). Output is ASCII-only and truncated by
default to reduce terminal injection risk.

``metavalidate`` reports decode/validation issues in text or JSON and emits
machine-readable issue codes (for example ``xmp/output_truncated`` and
``xmp/invalid_or_malformed_xml_text``) suitable for CI gating. For draft C2PA
verification, use ``--c2pa-verify-require-trusted-chain`` when an untrusted or
missing certificate chain must fail validation instead of being reported as a
separate chain-detail signal.

Python
------

Python bindings use nanobind. The wheel also ships helper scripts as
``openmeta.python.*`` modules.

.. code-block:: bash

   python3 -m openmeta.python.metaread file.jpg
   python3 -m openmeta.python.metadump --format portable file.jpg
   python3 -m openmeta.python.metadump file.jpg output.xmp
   python3 -m openmeta.python.metadump --format portable --c2pa-verify --c2pa-verify-backend auto file.jpg
   python3 -m openmeta.python.metadump --format portable --c2pa-verify --c2pa-verify-require-trusted-chain file.jpg
   python3 -m openmeta.python.metadump --format portable --portable-include-existing-xmp --xmp-sidecar file.jpg

``openmeta.python.metatransfer`` remains a thin command-line wrapper. Its
``--xmp-writeback``, ``--xmp-destination-embedded``,
``--xmp-destination-sidecar``, ``--output``, and ``--force`` flags map directly
onto the C++ file-helper options and persistence flags. It reports sidecar and
cleanup paths returned by the C++ result instead of deriving a separate
Python-side contract. Its ``--target-width``, ``--target-height``,
``--target-orientation``, ``--target-samples-per-pixel``,
``--target-bits-per-sample``, ``--target-sample-format``,
``--target-photometric``, ``--target-planar-configuration``,
``--target-compression``, and ``--target-exif-color-space`` flags populate the
same target image spec used by the C++ transfer request.

Resource policy defaults
------------------------

For C++ callers, initialize from ``recommended_resource_policy()`` and only
override fields you need:

.. code-block:: cpp

   #include "openmeta/resource_policy.h"
   openmeta::OpenMetaResourcePolicy policy
       = openmeta::recommended_resource_policy();
   policy.jumbf_limits.max_box_depth = 24;  // optional override

For JUMBF/C2PA preflight traversal checks, call
``measure_jumbf_structure(bytes, policy.jumbf_limits)`` before full decode.

Other preflight estimate APIs use the same bounded-options model:

- ``measure_scan_auto(file_bytes)``
- ``measure_scan_jpeg(bytes)``, ``measure_scan_png(bytes)``,
  ``measure_scan_webp(bytes)``, ``measure_scan_gif(bytes)``,
  ``measure_scan_tiff(bytes)``, ``measure_scan_jp2(bytes)``,
  ``measure_scan_jxl(bytes)``, ``measure_scan_bmff(bytes)``
- ``measure_exif_tiff(exif_bytes, exif_options)``
- ``measure_xmp_packet(xmp_bytes, xmp_options)``
- ``measure_icc_profile(icc_bytes, icc_options)``
- ``measure_iptc_iim(iptc_bytes, iptc_options)``
- ``measure_photoshop_irb(irb_bytes, irb_options)``
- ``measure_exr_header(exr_bytes, exr_options)``
- ``measure_jumbf_payload(jumbf_bytes, jumbf_options)``

Documentation build
-------------------

Sphinx docs require:

- ``doxygen``
- Python packages listed in ``docs/requirements.txt``

.. code-block:: bash

   uv pip install -r docs/requirements.txt
   cmake -S . -B build -DOPENMETA_BUILD_SPHINX_DOCS=ON
   cmake --build build --target openmeta_docs_sphinx
