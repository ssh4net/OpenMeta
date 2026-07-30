Interpretation Status
=====================

This page tracks how far OpenMeta has moved beyond raw metadata decoding into
meaningful interpretation. Interpretation means that decoded entries have
stable names, typed values, semantic groups, query shapes, and transfer-safety
classification that host applications can use directly.

Current overall status: **high, measured above 99%** for the public target
scope.
This is intentionally lower than decode coverage. Decode parity only proves
that metadata carriers and entries are visible; interpretation also requires
human-readable meaning and safe cross-format behavior.

100% acceptance gates
---------------------

For the declared target scope, an entry counts as covered when it has one
explicit outcome:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Gate
     - Requirement
   * - Decoded
     - The carrier and entry are visible in ``MetaStore``, or the decoder
       reports an explicit unsupported/limit/malformed reason.
   * - Named
     - The entry has a stable public name, or it is deliberately exposed as an
       unknown numeric/private field.
   * - Typed
     - The raw value shape is preserved as scalar, vector, matrix, table, bytes,
       text, or opaque blob.
   * - Interpreted
     - Known enum-like values, orientation states, geometry, exposure/gain,
       color, white-balance, lens-correction, RAW-processing, and
       source-private meanings are projected into public helpers or query
       candidates.
   * - Classified
     - Source-bound data is classified as portable, target-owned, source
       RAW-specific, vendor-private, computational, thermal, preview/face/
       stitch metadata, or opaque/lossless.
   * - Queryable
     - Host/UI workflows can find the interpreted meaning through focused query
       helpers with source entries, confidence, value shape, and normalized
       fields where available.
   * - Fuzzy-searchable
     - Optional search can tolerate bounded spelling and property-path near
       misses while reporting similarity and exact/fuzzy provenance without
       silently changing interpretation.
   * - Structured
     - Host code can consume query-backed interpretation records without
       reassembling raw query candidates manually.
   * - Conflict-aware
     - Duplicated cross-family concepts either have a documented precedence rule
       or surface enough source information for host conflict handling.

Measured target audit
---------------------

The current target audit measures **99.85% interpretation coverage** and
**99.77% query coverage**. The denominator contains only explicitly declared
semantic targets. Unknown private numeric IDs and intentionally opaque
payloads are reported separately, while malformed, undefined, and incomplete
values remain visible as residual gaps instead of receiving guessed meanings.
Fuzzy Search is measured separately and is not part of the query percentage.

Coverage matrix
---------------

.. list-table::
   :header-rows: 1
   :widths: 22 43 15 20

   * - Area
     - Current coverage
     - Readiness
     - Main remaining gap
   * - Standard EXIF/TIFF/DNG tag names and typed values
     - Standard tag names, common scalar/vector values, DNG crop/color/
       exposure/RAW-processing fields, GeoTIFF key names, EXIF 3.1 learning/
       development/correction/noise tag names, and common EXIF/TIFF/DNG
       numeric value-name helpers are available. Exposure time, aperture, ISO
       sensitivity, exposure bias, exposure program/mode, gain,
       correction/noise status values, and raw exposure-adjustment records now
       flow into concept candidates or stable display helpers where
       appropriate.
     - High, above 99% for declared enum targets.
     - Undefined enum codes and malformed field encodings intentionally remain
       raw.
   * - ICC profiles
     - ICC header/tag table decode plus interpreted ``desc``, text,
       signatures, XYZ, curves, named-color, measurement, viewing-condition,
       MFT/MAB/MBA, numeric array, and malformed/limit handling.
     - High, about 90-95%.
     - Full color-management policy remains host-owned; OpenMeta interprets
       profile metadata, not rendered color transforms.
   * - IPTC-IIM and portable XMP
     - IPTC datasets and XMP properties decode into typed entries, bounded
       EXIF/IPTC-to-XMP projection exists for transfer/writeback, and common
       descriptive EXIF/IPTC/XMP concepts such as title/headline,
       description/caption, creator/author, keywords/subject,
       rights/license, credit/source, and IPTC date/time promoted into
       cross-family created-date candidates are
       queryable with source-entry provenance. The ``Descriptive`` concept
       also reconciles equivalent legacy IPTC/Photoshop fields; preserves
       non-equivalent object-type, object-attribute, and subject references;
       scopes editorial status/update/action/cycle/language/fixture fields,
       content locations, prior-envelope references, originating software,
       and editorial contacts; and normalizes editorial release/expiration
       date-time pairs. Legacy image type/layout, rasterized caption, audio
       type/channel/content/rate/resolution/duration/outcue, and object-preview
       format/version/data datasets are interpreted as source-bound technical
       records; IPTC image layout is not conflated with EXIF rotation
       orientation. It also interprets IPTC Core accessibility/taxonomy
       fields, IPTC Extension controlled-vocabulary,
       registry, image-region entity records, and rectangle/circle/polygon
       boundary geometry with explicit pixel/relative units, XMP document
       identity, and XMP Media Management resource-reference, ingredient,
       history, manifest-item/reference, version/event, and bounded pantry
       records; and groups structured
       creator contacts, events, people, organizations, products,
       artwork/objects, encoded rights expressions, PLUS parties/assets/
       license policy, and model/property releases with record kind, record
       scope, language, transfer hint, and independent sensitivity. XMP
       ``CreatorTool`` is interpreted as source software, Camera Raw Settings
       requires the exact standard namespace, and unknown nested namespaces
       retain full URI identity in collision-safe
       ``nsu_<namespace-uri-hex>:`` path segments.
     - High, about 99%.
     - Deliberately unmodeled arbitrary pantry payloads and undocumented
       extension datasets remain bounded raw metadata.
   * - Orientation
     - EXIF/TIFF orientation query, LibRaw flip mapping, and generic
       orientation helpers for index, rotation degrees, mirrored state,
       dimension swap, rotation-only fallback, human-readable labels, and
       EXIF-vs-XMP conflict reporting in the LibRaw bridge.
     - High, about 90-95%.
     - Higher-level policy for resolving container and host pixel-orientation
       state remains host-specific.
   * - Geometry, crop, active area, and borders
     - DNG crop/active-area/masked-area tags, Phase One/Leaf geometry,
       Fujifilm RAF raw crop/zoom rectangles, Canon aspect/crop metadata,
       Nikon Capture crop bounds, Sony panorama crop margins, canonical border
       margins, vendor RAW-processing geometry buckets, and
       crop/border-style paths are queryable.
     - High, about 88-92%.
     - More vendor-specific normalized rectangles and stronger output contracts
       for ambiguous multi-tag geometry.
   * - Exposure and gain
     - Standard EXIF exposure time, f-number, exposure program/mode,
       photographic sensitivity, exposure bias, exposure index, gain control,
       selected DNG
       baseline/raw-preview gain fields, matching XMP paths, and selected
       decoded vendor/MakerNote exposure names are queryable and promoted into
       cross-family exposure roles. Standard EXIF exposure program/mode and
       gain-control values plus selected Canon/Nikon/Sony/Fujifilm/
       Pentax/Olympus/Panasonic/Phase One/Kodak/Minolta/Sigma/Samsung/Ricoh
       MakerNote values carry human-readable labels where stable. Capture
       exposure facts are marked safe, while raw/DNG
       exposure adjustments are marked unsafe for rendered-image transfer.
     - Medium-high, about 91-94%.
     - More vendor MakerNote exposure print conversions and richer per-vendor
       exposure/gain labels.
   * - Color, white balance, profiles, and matrices
     - DNG color/calibration/reduction/forward matrix groups, white-balance
       vector groups, EXIF color-space evidence, ICC header/tag entries, XMP
       ICC/profile fields, PNG profile text carriers, RAW color/source-
       processing safety buckets, transfer hints, per-family grouped vendor
       color/WB candidates, long-tail camera-to-XYZ/RGB, Canon ColorData
       source-color tables, style/color, and white-balance gain aliases, and
       cross-family concept candidates with full grouped value vectors are
       identified. ICC/profile and color-space records have a distinct
       ``color_profile`` semantic role, while camera RAW profile/look/
       tone-curve/style fields and vendor source color tables have a separate
       ``source_color_transform`` role marked unsafe for rendered-image
       transfer. Matrix/vector groups require numeric payloads with
       conservative minimum shapes before promotion.
     - Medium-high, about 86-92%.
     - Deeper camera/vendor color science interpretation is intentionally
       conservative, especially for rendered-image transfer.
   * - Lens correction and RAW processing
     - Lens-correction groups, black/white levels, linearization, RAW value
       curves, RAW linearity limits, RAW calibration curves, RAW curve control
       points, CFA/sensor layout, raw-storage identifiers, vendor RAW/source-
       processing buckets, creative/picture style, film simulation,
       dynamic-range, optical correction, raw-development, computational,
       thermal, and stitch/panorama aliases, per-family vendor raw-storage/
       sensor/computational/thermal/stitch/source-processing table candidates,
       transfer hints, transfer diagnostics, and concept candidates with
       grouped table/vector values are classified for query and transfer
       safety. Current public RAW curve coverage includes DNG linearization/
       linearity-limit tags plus conservative Sony, Nikon, Kodak, Panasonic,
       and Phase One/Leaf-style curve or calibration names where decoded
       metadata is visible. Sony model-specific correction-offset routing
       includes the current ILCE-7RM6 Tag9416 offsets where the decoded payload
       exposes numeric correction arrays. RAW concept candidates now expose a
       conservative RAW applicability state; descriptor-aware
       concept-resolution overloads let a host or decoder provide a
       ``MetadataRawDataDescriptor`` so curve/LUT-like entries can remain
       conditional, apply to stored RAW samples, require compressed RAW
       storage, require the primary RAW plane, or be marked not applicable for
       rendered, uncompressed, packed, or non-primary-plane data. Transfer
       preparation can also consume the descriptor and drop RAW-processing
       metadata when source pixels are already rendered. Lens-correction
       grouped tables require numeric payloads before promotion.
     - Medium-high, about 90-94%.
     - Long-tail per-model correction tables, exact blob/decoder-stage
       applicability, and richer numeric normalization.
   * - Vendor MakerNotes
     - Broad MakerNote naming and source-processing classification exists for
       common vendors and several live computational/thermal vendors. Unknown
       entries remain lossless and source-private subgroups distinguish
       preview, face geometry, computational, thermal, stitch/panorama,
       pixel-shift, multi-shot, composite, auto-lighting, RAW crop/active-area,
       source color-transform, source style/rendering aliases, lens-correction,
       raw-level processing data, and Phase One/Leaf RAW-processing fields
       handled by direct classification plus dedicated normalized helpers.
       Classified multi-field vendor groups now surface as grouped
       query/interpretation candidates where safe to expose structurally.
       Selected Canon/Nikon/Sony/Fujifilm/Pentax/Olympus/Panasonic/Casio/
       Phase One/Kodak/Minolta/Sigma/Samsung/Ricoh/Apple/FLIR/JVC/General
       Imaging/Reconyx/Microsoft/Motorola/Nintendo/Sanyo print conversions expose
       bounded human-readable labels, including expanded Canon sub-IFDs and
       CanonCustom fields, Canon ColorData source color-transform aliases,
       Nikon sub-IFDs and NikonSettings fields, NikonSettings
       source-processing aliases, NikonSettings On/Off residual labels, Nikon
       Active D-Lighting labels, decoded Fujifilm ``mk_fuji*`` including flash
       white-balance naming, native RAF firmware naming, Pentax sub-IFDs,
       Olympus main/focus/equipment fields, Casio Type2 fields, Panasonic
       long-tail main-table fields, Apple AE/AF/HDR/capture/camera-type
       fields, FLIR GPS-valid state, JVC quality, General Imaging macro state,
       Reconyx moon phase/weekday/flash/illumination/battery/trigger labels,
       Microsoft stitch camera-motion/map-type labels, Motorola
       ``CustomRendered`` labels, Nintendo category labels, Sanyo
       main/MOV public-context scalar labels, current Canon RF
       lens-type labels, current Nikon Z ``LensData0800`` ``LensID`` labels,
       and an ambiguous Pentax Sigma/Samsung/Tokina lens-family label.
       Version/firmware payloads have a separate bounded formatter path for
       selected standard EXIF byte-version fields, Nikon version-like
       contexts, Olympus packed firmware fields, and native RAF firmware fields
       so formatted versions are not confused with enum labels. Canon AF
       micro-adjustment fields are classified for lens-correction search, and
       Canon ambience-selection fields are classified as source-processing
       metadata.
       Ambiguous per-model, per-version, text/count, measurement, MP4 numeric,
       or value-type-dependent labels intentionally remain empty instead of
       guessing.
     - Medium-high, about 97%.
     - Remaining encrypted/custom settings, per-model private tables,
       remaining live-vendor scalar/string-coded fields, and per-model
       firmware formulas outside currently supported contexts.
   * - BMFF item graph, HEIF/AVIF/CR3, JUMBF, and C2PA
     - BMFF derived fields, brand-name fields including ``avif`` / ``avis`` /
       ``avio`` AVIF-compatible brands, item-info rows,
       item type/semantic labels and semantic aggregate counters for common
       metadata carriers, whole-scene item graph counts, bounded graph
       component summaries, bounded per-component rows with role text,
       ordered member item IDs, known/unknown and semantic node counts,
       isolated-state, primary membership, typed auxiliary/derived/thumbnail/
       content-description relation counts with direction-correct endpoint
       roles and named item-id aliases, alpha/depth/disparity/matte
       auxiliary counts, and conservative component policy text, component
       content-bound counts, primary component
       content-bound flags, primary component multi-image candidates and
       policy text, and conservative content-bound metadata / multi-image
       policy hints, primary
       metadata-carrier/C2PA/JUMBF flags when the primary item itself is a
       metadata item, primary sidecar counts/flags for
       linked metadata and image sidecars, separate primary inbound
       derived-item and outbound derived-source summaries, content-bound
       C2PA/JUMBF sidecar
       policy hints, compact primary-scene node/edge summaries with unique
       linked-item role buckets, per-role edge counters, and
       image/metadata/content-bound metadata node counts, bounded
       ``ipco`` property-container summary counts, bounded
       ``ipma`` item-property association rows and per-property-type
       association/primary/essential rollups, bounded relations, ``grpl``
       item-group rows with group semantics and ordered entity roles for
       ``altr``, ``ster``, and ``pymd``, per-group-type summaries, primary
       item-group
       memberships, bounded ``iloc``/``idat`` item-data layout summaries,
       bounded ``grid``/``iovl``/``iden`` construction descriptors with
       ordered source IDs, grid coordinates, overlay offsets/background,
       identity sources, recursive method-2 item-offset descriptor reads,
       descriptor-reference depth, and fail-closed graph cycle/missing-source/
       truncated-reference validity fields. ``tili`` image items expose
       bounded ``tilC`` version-0 dimensions, extra dimensions, property
       relationships, tile grids, expected tile counts, ``dref``/``deti``
       mapping, internal ``tile_item_type``/``tipa`` associations, external
       URL state, logical offset tables, explicit or sequentially inferred
       tile sizes, empty-tile state, separate core/layout/complete validity,
       and a source-bound container-graph concept. Primary
       item-location and derived-construction aliases, primary-linked roles with linked-item
       semantic aggregate counters, aux semantics, primary color/profile
       property summaries, primary display dimensions and transform summary,
       primary pixel aspect ratio, primary pixel component bit depth,
       clean-aperture rationals, JUMBF box labels, and draft C2PA/JUMBF
       structural fields are exposed.
     - High, about 98%.
     - Independently authored tiled-image conformance files and full C2PA
       manifest/policy semantics.
   * - Photoshop IRB
     - Raw resources are preserved and a bounded interpreted subset is decoded
       for fixed-layout resources, including Photoshop 2 info/color-table
       summaries, resolution/version/print data, print-flag bytes,
       border/background/effective-BW data, display info, grid/guide info,
       color sampler headers/records, descriptor-header summaries plus safe
       descriptor class-name/class-ID/item-count fields, bounded descriptor
       item bodies (``bool``, ``long``, ``comp``, ``doub``, ``UntF``,
       ``TEXT``, ``enum``, ``type``, ``GlbC``, opaque ``alis`` and ``tdta``
       byte counts) with type-name/type-code
       fields, parsed maximum depth, and parsed per-type counters, ordered
       bounded ``obj`` reference streams for property,
       class, enumerated, offset, identifier, index, and name forms with
       reference paths, depths, indices, values, and aggregate counters,
       plus empty and non-empty nested object/list summaries with item
       paths, depths, list indices, and parsed-value counts, for resources
       including layer comps, measurement scale, timeline info, sheet
       disclosure, HDR toning, print info, onion skins, count info, print
       info/style, path selection state, and origin path info, working-path
       and numbered clipping-path byte-count / record summaries, alpha
       names/identifiers, captions,
       QuickMask info, URL/list data, autosave strings, ``XMLData``,
       ImageReady XML text, Lightroom workflow text, thumbnail headers,
       channel options, clipping-path names, Macintosh PrintInfo,
       Macintosh NSPrintInfo, Windows DEVMODE, alternate duotone-color,
       alternate spot-color, and obsolete Photoshop tag byte counts, legacy
       halftone/transfer/duotone/EPS byte
       summaries, embedded IPTC/ICC/XMP/EXIF resource byte counts, and
       embedded IPTC/XMP/ICC payload decode where enabled.
     - Medium-high, about 90-94%.
     - Full Photoshop action execution semantics, opaque alias/raw payload
       interpretation, and long-tail resource interpretation.
   * - Semantic query and records
     - Query helpers expose raw matches, confidence, provenance, value shapes,
       normalized candidates, canonical crop/active-area rectangles, Fujifilm
       RAF raw crop/zoom rectangles, Canon/Nikon/Sony crop and border
       patterns, border margins, exposure/gain roles, selected
       vendor/MakerNote exposure-name aliases, per-family grouped vendor
       records, descriptive EXIF/IPTC/XMP concepts including exact
       rights/license/credit/source/editorial/accessibility/taxonomy/registry/
       image-region/document-identity/document-lineage/document-history/
       technical-image/audio/preview semantics, explicit color-profile
       records for EXIF/ICC/XMP/PNG profile carriers, explicit
       source-color-transform records for camera RAW profiles, looks, tone
       curves, Canon ColorData tables, and vendor source color tables,
       explicit ``raw_value_curve``, ``raw_linearity_limit``,
       ``raw_calibration_curve``, and ``raw_curve_control_points`` records,
       explicit computational/thermal/stitch/source-processing records
       including NikonSettings groups, expanded source color/style/lens/
       source-processing aliases including Canon AF micro-adjustment and
       ambience-selection fields, source-processing buckets, explicit
       ``container_graph`` concepts for BMFF content-bound metadata,
       derived-image construction, tiled-image configuration, and
       whole-scene/primary-component/
       per-component multi-image policy,
       structured interpretation records, and bounded cross-family concept
       resolution for orientation, date/time, exposure/gain,
       color/profile, GPS, descriptive fields, geometry, lens-correction,
       RAW-processing, and container graph policy with
       parsed date/time fields, matching EXIF ``OffsetTime*``/
       ``SubSecTime*`` provenance, bounded subsecond precision,
       normalized-instant conflict checks, IPTC created and digital-creation
       date/time plus XMP ``DateTimeDigitized`` promoted into cross-family
       date candidates, same-scope EXIF/XMP GPS timestamp assembly, distinct
       camera/destination/shown/created coordinate roles, scope-aware
       structured-location conflict handling, language-aware descriptive
       scalar conflicts, structured descriptive record kinds/scopes,
       independent sensitivity, GPS altitude-reference state and display
       token, canonical
       geometry
       origin/size/rect/margins, normalized exposure values, shape-checked
       grouped value vectors, transfer hints, RAW applicability states,
       rendered/compatible safety booleans, and tolerance-aware
       GPS/exposure/color/geometry conflicts.
     - High, measured about 99.77% for declared targets.
     - Incomplete GPS tuples, malformed dates, and undefined values remain
       inspection data rather than normalized candidates.
   * - Fuzzy Search
     - Optional ``fuzzy_search_metadata(...)`` searches decoded names and
       property paths with bounded score/result options, deterministic top-k
       ordering, stable ties, and exact/alias/fuzzy provenance. Python
       snapshot/document wrappers preserve the same result shape.
     - Medium-high, about 70-80%.
     - Broaden real-world typo/alias and negative corpora, then design and
       validate Unicode normalization, transliteration, and multilingual
       ranking.
   * - Transfer-safety classification
     - Compatible-file versus rendered-image safety policies classify
       source-specific image geometry, color/profile, RAW curves/linearity
       metadata, RAW-processing, MakerNote, BMFF content-bound/multi-image/
       derived-construction/tiled-image policy, JUMBF/C2PA, and vendor-private
       data, with
       concept-level diagnostics that report
       keep/drop/requires-target-image-spec actions, severity, stable summary
       and message tokens, localizable argument tokens, RAW applicability, and
       role-specific default message text before prepare. BMFF
       content-bound/multi-image scene, derived-image construction, and
       tiled-image configuration fields resolve to source-bound
       container-graph diagnostics for rendered-image transfers.
       Source-processing
       diagnostics distinguish computational, thermal, and stitch/panorama
       message tokens.
       Descriptor-aware diagnostics can also mark curve/LUT-like RAW roles as
       compressed-storage-only or primary-plane-only.
       ``PrepareTransferRequest`` can carry a
       ``MetadataRawDataDescriptor``; when it marks source pixels as rendered,
       RAW-processing metadata is filtered even under compatible-file safety.
     - High, about 94-96%.
     - More per-family policy tests and broader per-family policy coverage.

Competitor position
-------------------

ExifTool remains the practical reference for long-tail tag names, MakerNote
tables, and human-readable print conversions. OpenMeta is now close on decode
visibility for the current target scope, but interpretation still trails
ExifTool in per-model private meanings.

Exiv2 is strong for common EXIF/IPTC/XMP workflows. OpenMeta's differentiator
is the explicit safe-transfer and host-query model: it classifies whether data
is portable, target-owned, source RAW-specific, or unsafe to move into rendered
outputs.

Next interpretation priorities
------------------------------

1. Validate the complete bounded BMFF tiled-image contract against
   independently authored conformance files as they become available.
2. Broaden transfer diagnostic policy coverage now that stable message tokens
   and localizable argument tokens are available for GUI workflows.
3. Add pantry payload semantics only where a validated, bounded schema exists;
   keep arbitrary payloads raw and source-bound.
4. Expand the remaining unambiguous MakerNote long tail: encrypted/custom
   settings, per-model firmware formulas outside currently supported
   formatter contexts, remaining live-vendor scalar/string-coded fields, and
   per-model tables only where context is strong enough to avoid wrong labels.
5. Keep transfer-safety classification conservative when interpretation is
   incomplete.
