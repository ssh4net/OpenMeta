API Stability
=============

This page defines the adoption status for public OpenMeta APIs.
Python bindings mirror these labels unless a Python wrapper documents a
different status.

Stability levels
----------------

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Level
     - Meaning
   * - Stable
     - Intended for downstream use. Breaking changes require a new contract
       version, a compatibility path, or a documented migration.
   * - Experimental
     - Public and tested, but the exact shape or semantics may still evolve
       while the surrounding workflow is being hardened.
   * - Internal
     - Publicly visible only because it is part of a lower-level
       implementation surface. Do not build new downstream integrations on it
       unless another doc names it as supported.

Host-facing API map
-------------------

.. list-table::
   :header-rows: 1
   :widths: 24 24 14 38

   * - API surface
     - Header
     - Stability
     - Notes
   * - Runtime capability query: ``metadata_capability(...)``
     - ``openmeta/metadata_capabilities.h``
     - Stable
     - v1 query contract for read, structured decode, transfer preparation,
       target edit, and raw-preservation status by format/family.
   * - Compatibility dumps: ``dump_metadata_compatibility(...)``,
       ``dump_transfer_compatibility(...)``
     - ``openmeta/compatibility_dump.h``
     - Stable
     - Stable v1 line-oriented compatibility dump contract, including
       generic BMFF component membership, role, relation-summary, policy,
       derived-image construction, and tiled-image configuration fields as
       normal ``bmff_field`` entries.
       See
       :doc:`compatibility_dump`.
   * - XMP sync and writeback policy enums: ``XmpConflictPolicy``,
       existing-carrier precedence enums, ``XmpWritebackMode``, destination
       carrier modes
     - ``openmeta/xmp_dump.h``, ``openmeta/metadata_transfer.h``
     - Stable
     - Stable bounded writer policy for generated portable XMP. See
       :doc:`xmp_sync_policy`.
   * - Generic metadata traversal: ``visit_metadata(...)``,
       ``MetadataSink``, ``ExportOptions``, ``ExportItem``
     - ``openmeta/interop_export.h``
     - Stable
     - v1 traversal contract. Borrowed names are valid only during
       ``MetadataSink::on_item(...)``.
   * - ``ExportNameStyle::Canonical`` and
       ``ExportNameStyle::XmpPortable``
     - ``openmeta/interop_export.h``
     - Stable
     - Stable naming modes for key-space-aware and portable exports.
   * - ``ExportNameStyle::FlatHost``
     - ``openmeta/interop_export.h``
     - Stable
     - Stable v1 flat host naming contract. See :doc:`flat_host_mapping`.
   * - EXIF/TIFF orientation helpers: ``interpret_exif_orientation(...)``,
       ``exif_orientation_name(...)``,
       ``exif_orientation_rotation_degrees_cw(...)``,
       ``exif_orientation_rotation_only(...)``
     - ``openmeta/orientation.h``
     - Stable
     - Small utility contract for user-facing orientation labels, clockwise
       rotation degrees, mirrored-state detection, dimension-swap detection,
       and rotation-only fallbacks. Python exposes the same helpers through
       thin scalar/dictionary wrappers.
   * - EXIF/TIFF/DNG numeric value names and version formatting:
       ``exif_tag_numeric_value_name(...)``,
       ``exif_tag_numeric_value_format(...)``,
       ``exif_tag_byte_value_format(...)``, and focused helpers
     - ``openmeta/exif_value_names.h``
     - Stable
     - Small helper contract for common enum-like TIFF/EXIF/DNG numeric values
       such as compression, photometric interpretation, planar configuration,
       exposure program/mode, metering mode, light source, flash, color space,
       white balance, scene capture type, gain control, CFA layout, and DNG
       calibration illuminants, and EXIF 3.1 lens-correction /
       noise-reduction status values, plus selected bounded Canon/Nikon/
       Sony/Fujifilm/Pentax/Olympus/Panasonic/Phase One/Kodak/Minolta/Sigma/
       Samsung/Ricoh/Apple/FLIR/JVC/GE/Reconyx/Microsoft/Motorola/Nintendo/Sanyo
       MakerNote contexts including NikonSettings On/Off labels, Reconyx
       scalar labels, Microsoft stitch labels, Motorola ``CustomRendered``
       labels, Nintendo category labels, Sanyo public-context scalar labels,
       current Canon RF lens-type labels,
       current Nikon Z ``LensData0800`` ``LensID`` labels, and an ambiguous
       Pentax Sigma/Samsung/Tokina lens-family label where stable.
       Version/firmware helpers format selected standard EXIF byte-version
       fields, Nikon
       version-like payloads, Olympus packed firmware values, and native RAF
       firmware payloads without treating formatted versions as enum labels.
       Unknown or ambiguous values return an empty string or ``false`` and
       remain lossless metadata.
   * - Photoshop IRB decode: ``decode_photoshop_irb(...)`` and
       ``measure_photoshop_irb(...)``
     - ``openmeta/photoshop_irb_decode.h``
     - Experimental
     - Bounded resource traversal with stable raw resource preservation
       behavior, but the interpreted subset can still grow. Current
       interpretation includes fixed-layout resource fields,
       display/grid/thumbnail/color-sampler headers, working-path and
       numbered clipping-path byte counts / record summaries,
       descriptor-header summaries plus safe descriptor class-name/class-ID/
       item-count fields, bounded descriptor item bodies for ``bool``,
       ``long``, ``comp``, ``doub``, ``UntF``, ``TEXT``, ``enum``, ``type``,
       and ``GlbC``, opaque ``alis`` and ``tdta`` byte counts, descriptor item
       type-name/type-code fields, ordered bounded ``obj`` property, class,
       enumerated, offset, identifier, index, and name reference fields with
       per-value and aggregate limits,
       parsed maximum depth, and parsed per-type counters, nested
       object/list traversal with item path/depth/list-index and parsed-value
       count fields, ``XMLData``, ImageReady ASCII text resources, Lightroom
       workflow text,
       Macintosh PrintInfo, Macintosh NSPrintInfo, Windows DEVMODE,
       alternate duotone-color, alternate spot-color, and obsolete Photoshop
       tag byte counts,
       legacy halftone/transfer/duotone/EPS byte summaries, embedded
       IPTC/ICC/EXIF/EXIF2/XMP byte-count fields, and optional embedded
       IPTC-IIM, XMP, and ICC payload decode.
   * - Semantic metadata query: ``query_metadata(...)``,
       ``query_crop_metadata(...)``, focused query helpers, and
       ``metadata_query_fuzzy_search_available()``
     - ``openmeta/metadata_query.h``
     - Experimental
     - Query contract for inspection matches plus normalized candidates.
       Current coverage includes crop/active-area/border margins,
       exposure/gain, white balance, color/profile/source-color-transform,
       lens correction, orientation, descriptive EXIF/IPTC/XMP fields
       including exact contact/event/person/organization/product/artwork/
       rights/license/credit/source/rights-expression/release semantics,
       container-graph evidence for BMFF content-bound metadata and
       multi-image scene policy, and RAW/source-processing metadata across
       standard tags, selected DNG tags,
       RAW value curves, RAW linearity limits, RAW calibration curves, RAW
       curve control points, EXIF color-space evidence, ICC header/tag
       entries, XMP
       ICC/profile/color-space fields, XMP camera RAW profile/look/tone-curve
       fields, PNG profile text carriers, Fujifilm RAF raw crop/zoom
       rectangles, Canon aspect/crop metadata, Canon AF micro-adjustment,
       Canon ambience-selection, Canon ColorData source color-transform,
       NikonSettings source-processing aliases, Nikon Capture crop bounds,
       Sony panorama crop margins, selected decoded vendor/MakerNote exposure
       names, fuzzy XMP paths, and vendor RAW-processing classification.
       Matches report
       ``exact_match``,
       ``fuzzy_match``, and ``fuzzy_score`` so tools can label exact results
       separately from RapidFuzz near-miss hits.
       Exact rights/license/credit/source matches may have zero
       ``matched_terms`` because the stable 32-bit legacy term mask is full;
       their explicit semantic and confidence remain authoritative.
       ``OPENMETA_ENABLE_RAPIDFUZZ=ON`` adds optional near-miss
       XMP/property-path scoring. Grouped candidates
       include ``matrix_set``, ``vector_set``, and ``table`` shapes for related
       non-crop metadata, including RAW black/white levels, linearization, raw
       value curves, raw linearity limits, raw calibration curves, raw curve
       control points, CFA/sensor layout, source geometry, raw-storage
       identifiers, and source-processing buckets, and per-family vendor
       MakerNote/RAW white-balance, source-color-transform, raw-storage,
       sensor, computational, thermal, stitch/panorama, and source-processing
       groups.
       Matrix/vector/table groups are promoted only when the available numeric
       payloads meet conservative minimum shapes, so malformed color matrices,
       white-balance vectors, and lens-correction records remain per-entry
       inspection data instead of becoming normalized groups. Long-tail source
       color/style/lens/source-processing aliases such as camera-to-XYZ/RGB
       matrices, creative/picture style, film simulation, dynamic-range,
       optical-correction, AF micro-adjustment, ambience selection, Canon
       ColorData, NikonSettings, and raw-development terms are classified for
       query and transfer-policy inspection; camera RAW profiles, looks, tone
       curves, and vendor source color tables use the explicit
       ``source_color_transform`` semantic, RAW
       curve/linearity metadata uses dedicated RAW-processing semantics, and
       computational, thermal, and stitch/panorama fields use explicit
       source-processing subroles.
       Python ``Document`` and ``TransferSourceSnapshot`` mirror this as thin
       dictionary-returning wrappers.
   * - Structured metadata interpretation records:
       ``interpret_metadata(...)`` and ``interpret_metadata_query(...)``
     - ``openmeta/metadata_interpretation.h``
     - Experimental
     - Thin structured projection over semantic query candidates. Records
       carry query class, semantic kind, normalized shape, confidence, source
       entry ids, and normalized origin/size/rect/margins/value arrays where
       available. Current scope covers orientation, geometry/crop/border
       including Fujifilm RAF, Canon, Nikon Capture, and Sony panorama
       geometry patterns, exposure/gain,
       color/white-balance/profile/source-color-transform records,
       lens-correction, and RAW/source-processing records including raw value
       curves, linearity limits, calibration curves, curve control points,
       computational, thermal, and stitch/panorama subroles, and grouped
       vendor-family table/vector records where
       classification supports them. Python ``Document`` and
       ``TransferSourceSnapshot`` expose matching dictionary wrappers.
   * - Cross-family concept resolution:
       ``resolve_metadata_concepts(...)`` and
       ``resolve_metadata_concept(...)``
     - ``openmeta/metadata_concepts.h``
     - Experimental
     - First bounded resolver for duplicated host-facing concepts. Current
       scope reports candidates, candidate source entries, source families,
       preferred entries, normalized numeric/text keys, full normalized value
       vectors, transfer hints, RAW applicability states, normalized date/time
       fields, date/time precision including bounded subsecond digits,
       timezone kind, normalized geometry fields, normalized exposure values,
       and same-role conflicts for orientation,
       date/time, exposure/gain, color/profile/source-color-transform, GPS,
       descriptive fields, geometry, lens-correction, RAW-processing, and
       container-graph evidence
       across EXIF, XMP, IPTC, ICC, PNG text, BMFF fields, and query-backed
       interpretation records where applicable. Exposure
       candidates cover exposure time, aperture, ISO sensitivity, exposure
       bias, exposure program/mode, gain, and raw exposure-adjustment roles
       across standard EXIF/DNG/XMP evidence and selected decoded
       vendor/MakerNote exposure names. Standard EXIF exposure program/mode
       and gain-control values plus selected Canon/Nikon/Sony/Fujifilm/
       Pentax/Olympus/Panasonic/Phase One/Kodak/Minolta/Sigma/Samsung/Ricoh
       MakerNote values include human-readable labels where stable. Capture
       exposure facts are safe, while raw/DNG exposure adjustments stay
       rendered-unsafe. Geometry
       candidates cover crop, active area, border, and sensor geometry with
       canonical origin, size, rect, and margin fields when available,
       including normalized DNG, Phase One/Leaf, Fujifilm RAF, Canon, Nikon
       Capture, and Sony panorama geometry patterns.
       Candidate transfer hints distinguish ``safe``, ``source_bound``,
       ``rendered_unsafe``, and ``requires_target_image_spec`` evidence, with
       compatible-file and rendered-image safety booleans.
       Color/white-balance, source-color-transform, lens-correction,
       RAW-processing, and container-graph concepts preserve source evidence
       for host inspection; source-bound color transforms and RAW
       curve/linearity/calibration roles are marked rendered-unsafe,
       computational, thermal, and stitch/panorama RAW-processing roles are
       marked source-bound, and BMFF whole-scene, primary-component, and
       per-component content-bound metadata / multi-image policy plus
       derived-image construction and tiled-image configuration fields are
       marked source-bound.
       RAW curve/LUT-like concept roles are conservatively marked
       ``conditional_on_raw_encoding`` until a raw data descriptor can confirm
       whether they affect the stored samples. Descriptor-aware overloads
       accept ``MetadataRawDataDescriptor`` and can collapse supported
       stored-RAW descriptors to ``applies_to_stored_raw`` or rendered
       descriptors to ``not_applicable_to_stored_raw``, or compressed-only
       descriptors to not-applicable when the supplied storage encoding is
       uncompressed or packed, and primary-plane-only descriptors to
       not-applicable when the supplied plane index is non-primary.
       Matching EXIF ``OffsetTime*`` and ``SubSecTime*`` entries are assembled
       with ``DateTime*`` candidates. Offset-aware conflicts compare
       normalized UTC instants, while missing timezone or subsecond fields
       remain lower-precision evidence. EXIF and XMP GPS date/time are
       combined from same-scope ``GPSDateStamp`` plus ``GPSTimeStamp``
       entries, IPTC digital-creation date/time and XMP
       ``DateTimeDigitized`` map to the ``Digitized`` date/time role, and GPS
       altitude candidates expose altitude-reference code plus
       below-sea-level state when reference metadata is present. Camera
       position, EXIF/XMP destination coordinates, and IPTC Extension
       ``LocationShown`` / ``LocationCreated`` coordinates use distinct roles.
       Structured candidates expose ``location_scope``, and
       conflict/preference handling compares values only within the same scope;
       ``metadata_concept_gps_altitude_reference_name(...)`` provides a stable
       display token for the reference code. The ``Descriptive`` kind
       reconciles standard title, headline, description, creator,
       keyword/subject, created/shown location, copyright, rights/license,
       credit, and source fields. Localized scalars compare per normalized
       language; creator, keyword, location-identifier, rights-holder, and
       licensor collections preserve distinct values and select one preferred
       source per duplicate normalized value. Structured descriptive members
       expose ``record_kind`` and ``record_scope`` so related values remain
       associated, plus policy ``sensitivity`` independent of technical
       transfer safety. It is intended for
       inspection UI
       and host policy decisions; it does not rewrite metadata or hide
       ambiguity. Python ``Document`` and ``TransferSourceSnapshot`` expose
       matching dictionary wrappers, including subsecond, location-scope, and
       record-scope and language fields and the thin
       ``MetadataRawDataDescriptor`` object with
       storage, compression, and plane-binding fields.
   * - Transfer concept diagnostics:
       ``transfer_concept_diagnostics_from_store(...)``,
       ``transfer_concept_diagnostic_message(...)``,
       ``transfer_concept_diagnostic_token(...)``,
       ``transfer_concept_diagnostic_message_token(...)``,
       ``transfer_concept_diagnostic_message_arguments(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Preflight view over concept candidates for ``TransferSafetyMode``.
       Each diagnostic reports concept kind/role, transfer hint,
       keep/drop/requires-target-image-spec action, reason token, severity
       token, stable host-facing summary token, stable localization message
       token, localizable argument tokens, default message text, conflict
       flag, source entries, structured-location scope, generic
       structured-record scope, and language where applicable,
       compatible/rendered safety booleans, RAW applicability state, and GPS
       altitude-reference presentation fields. Descriptor-aware overloads
       accept ``MetadataRawDataDescriptor`` and make RAW-processing keep/drop
       decisions reflect the supplied stored-RAW, compressed-only,
       primary-plane-only, or rendered storage context.
       ``PrepareTransferRequest::source_raw_data_descriptor`` can apply the
       same coarse rendered-source RAW filtering during
       ``prepare_metadata_for_target(...)``; it is still intentionally
       conservative and does not prove vendor curve/LUT activity for a
       specific compression mode or decoder stage.
       Rendered-transfer drop messages distinguish source color transforms,
       white balance, lens-correction records, source RAW curves/linearity
       metadata that still require storage-context confirmation, and BMFF
       content-bound/multi-image scene/derived-image construction/tiled-image
       configuration policy, plus computational/thermal/stitch
       source-processing drops from generic source-processing metadata.
       Python ``Document`` and ``TransferSourceSnapshot`` expose
       ``transfer_concept_diagnostics(...)``
       dictionaries with ``severity_name``, ``token``, ``message_token``,
       ``message_arguments``, ``message``, ``location_scope``,
       ``record_scope``, ``language``,
       and RAW applicability fields, with
       overloads that accept the thin
       ``MetadataRawDataDescriptor`` object. Python transfer helpers also
       accept ``source_raw_data_descriptor`` for prepare-time filtering.
   * - Vendor RAW-processing summaries:
       ``vendor_raw_processing_from_store(...)``,
       ``classify_vendor_raw_processing_field(...)``
     - ``openmeta/vendor_raw_processing.h``
     - Experimental
     - Conservative grouped source-RAW/source-processing field summaries for decoded Sony,
       Canon, Nikon, Fujifilm, Pentax, Panasonic, Olympus, Kodak, Minolta,
       Sigma, Samsung, Ricoh, Apple, DJI, Google, FLIR, Casio, Sanyo,
       KyoceraRaw, Reconyx, HP, JVC, GE, Motorola, Nintendo, and Microsoft
       MakerNotes, including vendor-private, computational, thermal, preview,
       face-geometry, stitch/panorama, Apple computational capture/HDR/motion,
       DJI pose/thermal, Google HDR+/shot-log, pixel-shift/multi-shot/
       composite/auto-lighting/source-style processing, and FLIR
       radiometric/raw-value buckets. Long-tail aliases cover source color/style,
       camera-to-XYZ/RGB matrix, white-balance gain, optical/lens correction,
       dynamic-range, and raw-development terms. Direct field classification
       also recognizes decoded Phase One/Leaf RAW-processing tags; use the
       dedicated Phase One/Leaf helpers for normalized geometry and processing
       summaries. Intended for audit/UI and rendered-transfer safety decisions,
       not for writing vendor RAW/source-processing values into rendered targets.
   * - Transfer safety audit:
       ``transfer_safety_audit_from_store(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Preflight summary of source entries and entries filtered or invalidated
       by ``TransferSafetyMode``, including
       Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/
       Sigma/Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/
       Reconyx/HP/JVC/GE/Motorola/Nintendo/Microsoft RAW/source-processing
       buckets. Intended for diagnostics and host UI before preparing
       rendered-image transfers.
   * - Raw-carrier passthrough audit:
       ``raw_carrier_passthrough_audit_from_snapshot(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Diagnostic preflight for opt-in raw carriers. Reports candidate
       carriers and primary block reasons such as missing payload, target
       incompatibility, safety filtering, content-bound C2PA, explicit profile
       policy, missing decoded-entry links, or unsupported carrier kind. Does
       Hosts can call it directly before enabling snapshot passthrough.
   * - Source snapshot type and read helpers:
       ``TransferSourceSnapshot``,
       ``read_transfer_source_snapshot_file(...)``,
       ``read_transfer_source_snapshot_bytes(...)``,
       ``build_transfer_source_snapshot(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Current snapshots are decoded-store-backed by default. Opt-in raw
       carriers preserve bounded source payload/provenance records and
       snapshot-local decoded entry ids for host diagnostics and bounded
       passthrough decisions. Const reuse is safe when callers do not mutate
       the snapshot and do not share returned result objects across writers.
   * - Fileless preparation:
       ``prepare_metadata_for_target_snapshot(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Intended for hosts that already decoded metadata and want to prepare
       transfer artifacts without reopening the source file.
       ``TransferRawCarrierPassthroughMode::WhenSafe`` is an opt-in snapshot
       mode; the current writer path only reuses eligible non-C2PA JUMBF and
       draft unsigned C2PA invalidation carriers for JPEG, JXL, and BMFF
       targets, plus draft unsigned C2PA invalidation carriers for WebP.
   * - Snapshot execution:
       ``execute_prepared_transfer_snapshot(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Intended for deferred save/writeback from a reusable decoded source
       snapshot.
   * - Bundle execution:
       ``execute_prepared_transfer_bundle(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Intended for hosts that already own a prepared bundle and destination
       bytes. Treat bundles as immutable except through documented patch
       helpers.
   * - Adapter-view execution:
       ``build_prepared_transfer_adapter_view(...)``,
       ``emit_prepared_transfer_adapter_view(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Target-neutral operation view for host-owned encoders and writers.
       Route and dispatch details may still evolve.
   * - Generated transfer payload internals, route strings, low-level package
       chunks, and diagnostic counters not documented by a stable API page
     - ``openmeta/metadata_transfer.h``
     - Internal
     - These fields may be useful for tests and diagnostics, but they are not a
       compatibility contract for downstream integrations.

Structured descriptive record kinds cover creator contacts, events, people,
organizations, products, artwork/objects, rights expressions, rights holders,
licensors, licensees, licenses, releases, end users, image creators, image
suppliers, image assets, controlled-vocabulary terms, registry entries, image
regions, resource references, resource events, manifest items, versions,
editorial workflows, source software, editorial contacts, technical-image,
audio-asset, preview-asset, and pantry records. The IPTC technical records
normalize image layout, component count, audio channel/rate/resolution/
duration, preview format/version, and bounded binary payload identity without
treating IPTC layout as EXIF rotation.
Sensitivity
is mirrored in transfer diagnostics and thin Python dictionaries. Hosts must
not interpret a ``safe`` transfer
hint as approval to publish personal-contact,
person-identity, location, or legal-rights metadata.

Exact descriptive query semantics also cover legacy editorial workflow pairs,
non-equivalent IPTC taxonomy and workflow fields, scoped prior-envelope
references and originating software, IPTC Core accessibility and taxonomy
fields, IPTC Extension registry and
image-region entities, resource/document identity and lineage/history, and
remaining bounded PLUS party, delivered-asset, and license-policy fields.
Legacy IPTC image, audio, and preview datasets use exact
``technical_image``, ``audio``, and ``preview`` semantics.
Equivalent scalar pairs participate in preference/conflict handling; taxonomy,
resource-identifier, and license-document collections remain additive.
Document identity/lineage/history, registry, prior-envelope-reference, and
source-software records are source-bound for rendered transfer; image-region
records require target image specifications. Editorial contacts carry
personal-contact sensitivity independently of their technical transfer hint.
Technical-image, audio-asset, and preview-asset records are source-bound.

The bounded BMFF tiled-image field contract covers ``tilC`` version 0 tile
dimensions, up to eight extra dimensions, ``dref``/``deti`` mapping, internal
``tile_item_type``/``tipa`` associations, bounded external URL components,
logical offset-table rows, explicit or sequentially inferred tile sizes, and
separate core/layout/complete validity. Offset-table validation is capped at
262144 entries, emitted rows at 64, property associations at 64, and retained
URL components at 512 bytes. ``TiledImageConfiguration`` remains an
experimental source-bound concept role exposed unchanged by the thin Python
enum.

Practical guidance
------------------

Use stable APIs for normal application integrations. Use experimental APIs when
they match a real workflow and the integration can track OpenMeta releases.
Avoid internal surfaces unless you are contributing to OpenMeta itself or
writing a test that is intentionally tied to implementation details.
