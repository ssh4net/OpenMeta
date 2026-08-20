Host Integration
================

This guide is for applications that already own the image or container side
and want OpenMeta to handle metadata.

OpenMeta is not an image encoder. The normal pattern is:

- decode metadata from one source file
- query or edit it in ``MetaStore``
- hand prepared metadata to your own writer, encoder, or SDK

If you want the shortest end-to-end examples first, start with
:doc:`quick_start`.
For public API adoption status, see :doc:`api_stability`.
For the stable flat host naming contract, see :doc:`flat_host_mapping`.
For deterministic host compatibility baselines, see :doc:`compatibility_dump`.
For generated XMP merge and writeback precedence, see
:doc:`xmp_sync_policy`.
For per-target writer preserve/replace guarantees, see
:doc:`writer_target_contract`.

Pick the integration path
-------------------------

Use the narrowest public API that matches your host:

=============================== ===========================================================
Host owns                       Use
=============================== ===========================================================
Existing target file/template   ``execute_prepared_transfer_file(...)`` +
                                ``persist_prepared_transfer_file_result(...)``
EXR writer                      ``build_exr_attribute_batch_from_file(...)``
Host-owned metadata object      ``visit_metadata(...)``
Host inspection/search UI       ``openmeta/metadata_query.h`` focused query
                                helpers
Structured interpreted records  ``openmeta/metadata_interpretation.h``
Cross-family concept conflicts  ``openmeta/metadata_concepts.h``
User-facing orientation display ``openmeta/orientation.h``
Common EXIF/TIFF/DNG and        ``openmeta/exif_value_names.h``
selected MakerNote value labels
JPEG/JXL/WebP/PNG/JP2/BMFF      ``prepare_metadata_for_target_file(...)`` +
encoder path                    adapter view or backend emitter
Adobe DNG SDK objects/files     ``dng_sdk_adapter.h``
=============================== ===========================================================

For inspection/search UI, prefer the experimental semantic query helpers before
building a separate fuzzy layer. They report source entries, confidence, value
shape, exact/fuzzy match provenance, and normalized candidates while preserving
ambiguity.

For host code that wants a simpler iterable result, use
``metadata_interpretation.h``. It keeps the same semantic vocabulary as query
but returns structured records with query class, normalized shape, source
entries, confidence, and normalized geometry/value arrays.

For host code that needs to reconcile duplicated concepts across metadata
families, use ``metadata_concepts.h``. It reports orientation, date/time,
exposure/gain, color/profile, GPS, geometry, lens-correction, and
RAW-processing candidates with source families, preferred entries, and
same-role conflict flags. Exposure candidates expose capture facts such as
exposure time, aperture, ISO, exposure bias, exposure program, and gain as
safe transfer facts, while raw/DNG exposure adjustment fields remain unsafe
for rendered-image transfer. Geometry candidates expose crop, active-area,
border, and sensor-geometry roles with canonical origin, size, rect, and
margin fields when available, including known DNG, Phase One/Leaf, Fujifilm
RAF, Canon, Nikon Capture, and Sony panorama geometry patterns. Color/white
balance, lens-correction, and RAW-processing candidates expose full normalized
value vectors for grouped matrix/vector/table records when the source payloads
satisfy conservative numeric shape checks. Malformed or text-only source
records remain visible as individual metadata, but they are not promoted into
normalized grouped color, white-balance, or lens-correction candidates.
Date/time candidates include parsed date/time fields when the source value is
recognizable, plus precision and timezone-kind fields. Matching EXIF
``OffsetTime*`` and ``SubSecTime*`` companions are assembled with their
``DateTime*`` values; normalized subseconds are bounded to nine decimal
digits. Offset-aware candidates are compared as UTC instants, so equivalent
timestamps with different written offsets do not conflict. IPTC created
date/time, IPTC digital-creation date/time, XMP digitized timestamps, and GPS
timestamps are assembled where the source fields provide enough pieces. GPS
timestamps combine ``GPSDateStamp`` with ``GPSTimeStamp`` only within the same
XMP property scope, and GPS altitude candidates report whether
``GPSAltitudeRef`` marked the height as below sea level. Camera position accepts
XMP coordinates only from the EXIF XMP schema. EXIF/XMP destination coordinates
and IPTC Extension ``LocationShown`` / ``LocationCreated`` coordinates use
distinct roles, so they are not conflated with camera position.
Structured-location candidates carry a ``location_scope`` such as
``LocationShown[1]``; conflicts and preference are resolved independently
within each scope. Use
``metadata_concept_gps_altitude_reference_name(...)`` for a stable display
token. Descriptive concepts reconcile standard EXIF, IPTC IIM, Dublin Core,
XMP Rights, Photoshop, IPTC Core/Extension, and PLUS title/headline,
description, creator, keyword/subject, location, copyright, rights/license,
credit, source, creator-contact, event, person, organization, product,
artwork/object, encoded-rights-expression, license-constraint, release,
legacy editorial, accessibility, taxonomy, resource/document identity,
controlled-vocabulary terms, registry entries, image-region entities,
document lineage/history, end-user, image-creator, image-supplier, and
delivered-image fields. Scalar
conflicts are isolated by normalized language and structured location scope;
additive collections retain distinct values. Structured
members carry both a ``record_kind`` and ``record_scope``, such as ``person``
plus ``Person[1]``, so hosts can keep associated values together. IPTC image
boundaries use the ``image_region_boundary`` record kind. A complete boundary
adds a ``region_boundary`` candidate with explicit ``image_region_shape`` and
``image_region_coordinate_unit`` fields. Rectangle values use ``rect`` as x,
y, width, height; circles use a three-value x, y, radius vector; polygons use
a flattened x, y ``vector_set`` plus scoped ``vec2`` vertex candidates.
OpenMeta does not synthesize geometry when the shape, unit, or required
coordinates are incomplete, but valid individual numeric fields remain
queryable. Pixel and relative coordinates are not interchangeable: hosts must
use source and target image specifications to transform or validate them
before transfer. Treat this as an inspection and policy input
rather than an automatic
metadata rewrite decision; source-bound color, lens, and RAW-processing values
still need rendered-transfer safety filtering. Document identity, registry,
XMP Media Management manifest/version/lineage/history records, and pantry
identity are source-bound; image-region records require target image
specifications because their meaning is tied to the source image. Each
candidate also carries a
transfer hint: ``safe``, ``source_bound``, ``rendered_unsafe``, or
``requires_target_image_spec``, plus ``compatible_file_safe`` and
``rendered_image_safe`` booleans for host UI and preflight policy. For transfer
previews, ``transfer_concept_diagnostics_from_store(...)`` converts those hints
into keep/drop/requires-target-image-spec actions for a selected
``TransferSafetyMode``, plus stable severity tokens, summary tokens, message
tokens, argument tokens, and default message text for host UI. If the host
knows the source storage context, pass
``MetadataRawDataDescriptor`` to the descriptor-aware overload so
RAW-processing diagnostics distinguish stored RAW samples from rendered pixels
before choosing keep/drop actions. Rendered-transfer drop messages distinguish
source color transforms, white balance, lens correction, source RAW
curves/linearity metadata, source-bound RAW processing, and target-owned image
properties. Hosts can localize or replace the wording, but they do not need to
invent the basic safe/drop/rewrite reasons.

``sensitivity`` is a separate policy signal with ``none``,
``personal_contact``, ``person_identity``, ``location``, and ``legal_rights``
values. A candidate marked ``safe`` can still be privacy- or policy-sensitive.
Hosts that publish or share images should review or remove sensitive metadata
according to their own user-consent, privacy, and rights policies.

Adapter classes
---------------

OpenMeta splits host integration surfaces deliberately:

- export-only naming/traversal surface:
  ``visit_metadata(...)`` for host-owned metadata mapping layers
- export-only adapter:
  ``build_ocio_metadata_tree(...)`` for OCIO-style metadata trees
- host-apply adapter:
  ``build_exr_attribute_batch(...)`` for EXR/OpenEXR header workflows
- direct bridge:
  ``dng_sdk_adapter.h`` for applications that already use Adobe DNG SDK
  objects
- narrow translator:
  ``libraw_adapter.h`` for orientation mapping into LibRaw flip space
- orientation utility:
  ``orientation.h`` for EXIF/TIFF labels, rotation degrees, mirrored-state
  checks, and width/height-swap checks
- value-name utility:
  ``exif_value_names.h`` for common EXIF/TIFF/DNG enum-style numeric labels
  and selected bounded Canon/Nikon/Sony/Fujifilm/Pentax/Olympus/Panasonic/
  Phase One/Kodak/Minolta/Sigma/Samsung/Ricoh MakerNote labels
- structured interpretation utility:
  ``metadata_interpretation.h`` for query-backed semantic records
- concept-resolution utility:
  ``metadata_concepts.h`` for cross-family orientation, date/time,
  color/profile, exposure/gain, GPS, descriptive fields, geometry,
  lens-correction, and RAW-processing conflict inspection

Read and query
--------------

The basic read path is covered in :doc:`quick_start`. Once you have a
``MetaStore``, the main lookup API is exact-key lookup through
``MetaStore::find_all(...)``.

Use exact lookup for deterministic key access. For inspection/search UI, prefer
``openmeta/metadata_query.h`` before building a separate layer. It returns
source entries, confidence, value shape, and normalized candidates. Semantic
Query uses built-in tags and conservative namespace/name rules; it
intentionally does not use partial matching because near names must not alter
classification. Raw matches retain ``exact_match``, ``fuzzy_match``, and
``fuzzy_score`` compatibility fields.

For a free-text search box, use ``openmeta/metadata_fuzzy_search.h`` instead of
merging ranking policy into semantic Query. ``fuzzy_search_metadata(...)``
searches decoded names and property paths with explicit score and result
bounds. It returns deterministic top-k results with exact, alias, or fuzzy
provenance and stable entry-id tie-breaking. The current normalization contract
is locale-independent ASCII; non-ASCII queries return
``UnsupportedQueryText``, with no implicit transliteration. Thin Python
``Document`` and ``TransferSourceSnapshot`` wrappers return the same status,
counts, truncation flag, and match fields. Calls keep local state and may run
concurrently against an immutable finalized store. The current bounded linear
scan is intended for ordinary per-image stores; see :doc:`fuzzy_search` for
benchmark results and the deferred immutable-index boundary.

Random-access source foundation
-------------------------------

For realtime and large-asset pipelines,
``openmeta/random_access_source.h`` defines a borrowed fixed-size source with
either caller-owned contiguous memory or an exact synchronous
``read_at(offset, destination)`` callback. The primitive has no hidden
allocation, lock, scheduler, or file-position state. Request-count, total-byte,
and single-read ceilings use caller-owned accounting state.

Version 0.4.101 routes classic TIFF, BigTIFF, DNG, RW2, and ORF header/IFD
decoding through this contract with caller-owned structural and value scratch.
Version 0.4.102 adds source-backed Nikon embedded TIFF/type 1, Sony
outer-TIFF-relative IFDs, and contained Canon adjusted-base payloads. Version
0.4.103 adds Olympus nested IFDs, Panasonic binary tables, and Samsung
STMN/Type2 derived tables. Version 0.4.104 adds Fujifilm self-relative IFDs and
General Imaging Type 2 source windows. Version 0.4.105 adds Kodak fixed-layout
records and outer-TIFF-relative Type 8, Type 10, and Type 11 IFDs with vendor
subtables. Contiguous input retains the full existing decoder behavior.
Unconverted mixed-base vendor payloads and derived subtables remain explicit
residuals, so hosts must check
``ExifRandomAccessDecodeResult::complete()`` before claiming full nested parity.
See :doc:`random_access_input` for buffer sizing, lifetime, short-read,
concurrency, and performance requirements.

Generic host metadata traversal
-------------------------------

Use the traversal API when your application owns the metadata object model and
needs deterministic exported names plus the original ``Entry``.

.. code-block:: cpp

   #include "openmeta/interop_export.h"

   class MyMetadataSink final : public openmeta::MetadataSink {
   public:
       void on_item(const openmeta::ExportItem& item) noexcept override
       {
           // Map item.name + item.entry into your host metadata object.
       }
   };

   openmeta::ExportOptions options;
   options.style              = openmeta::ExportNameStyle::FlatHost;
   options.name_policy        = openmeta::ExportNamePolicy::Spec;
   options.include_makernotes = false;

   MyMetadataSink sink;
   openmeta::visit_metadata(store, options, sink);

This keeps host-specific object ownership and write behavior outside OpenMeta.
Use ``ExportNamePolicy::Spec`` for specification names such as
``Exif:ISOSpeedRatings`` and ``Exif:ExposureBiasValue``. Typed writeback uses
``import_flat_host_metadata(...)``. Existing values can use an exact source
entry id or a unique flat name; new entries require an explicit ``MetaKeyView``.
Ambiguous duplicate names are rejected.

EXR attribute batches
---------------------

This is the cleanest host-adapter path in OpenMeta today.

.. code-block:: cpp

   #include "openmeta/exr_adapter.h"

   openmeta::ExrAdapterBatch batch;
   openmeta::BuildExrAttributeBatchFileOptions options;

   openmeta::BuildExrAttributeBatchFileResult result =
       openmeta::build_exr_attribute_batch_from_file(
           "source.jpg", &batch, options);

   for (const openmeta::ExrAdapterAttribute& attr : batch.attributes) {
       // Forward attr.name, attr.type_name, and attr.value to your EXR writer.
   }

OpenMeta does not need OpenEXR headers for this path. It exports a neutral
batch of EXR-style attributes that your host can apply through OpenEXR or its
own EXR writer.

Host-owned JPEG or JXL output
-----------------------------

There are two public patterns for encoder-owned output:

- implement a backend emitter such as ``JpegTransferEmitter`` or
  ``JxlTransferEmitter``
- build an adapter view and consume one normalized list of operations

Adapter-view pattern
~~~~~~~~~~~~~~~~~~~~

Use this when you want one target-neutral operation list.

.. code-block:: cpp

   #include "openmeta/metadata_transfer.h"

   class MySink final : public openmeta::TransferAdapterSink {
   public:
       openmeta::TransferStatus
       emit_op(const openmeta::PreparedTransferAdapterOp& op,
               std::span<const std::byte> payload) noexcept override
       {
           // Dispatch on op.kind and forward payload into your backend.
           return openmeta::TransferStatus::Ok;
       }
   };

   openmeta::PrepareTransferFileOptions prepare;
   prepare.prepare.target_format = openmeta::TransferTargetFormat::Jxl;

   openmeta::PrepareTransferFileResult prepared =
       openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

   openmeta::PreparedTransferAdapterView view;
   openmeta::build_prepared_transfer_adapter_view(
       prepared.bundle, &view, openmeta::EmitTransferOptions {});

   MySink sink;
   openmeta::emit_prepared_transfer_adapter_view(prepared.bundle, view, sink);

Backend-emitter pattern
~~~~~~~~~~~~~~~~~~~~~~~

Use this when your host already looks like one OpenMeta backend.

.. code-block:: cpp

   #include "openmeta/metadata_transfer.h"

   class MyJpegEmitter final : public openmeta::JpegTransferEmitter {
   public:
       openmeta::TransferStatus
       write_app_marker(uint8_t marker_code,
                        std::span<const std::byte> payload) noexcept override
       {
           // Write one APPn marker into your JPEG output path.
           return openmeta::TransferStatus::Ok;
       }
   };

   openmeta::PrepareTransferFileOptions prepare;
   prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;

   openmeta::PrepareTransferFileResult prepared =
       openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

   openmeta::PreparedTransferExecutionPlan plan;
   openmeta::compile_prepared_transfer_execution(
       prepared.bundle, openmeta::EmitTransferOptions {}, &plan);

   MyJpegEmitter emitter;
   openmeta::emit_prepared_transfer_compiled(prepared.bundle, plan, emitter);

For JXL, implement ``JxlTransferEmitter::set_icc_profile(...)``,
``add_box(...)``, and ``close_boxes(...)``.

OpenMeta does not ship a TurboJPEG-specific wrapper yet. The intended
integration path is still through ``JpegTransferEmitter`` or the adapter view.

Edit an existing target file
----------------------------

If your host already has a target file or template on disk, use the file
helper instead of building your own writer path.

.. code-block:: cpp

   #include "openmeta/metadata_transfer.h"

   openmeta::ExecutePreparedTransferFileOptions exec_options;
   exec_options.prepare.prepare.target_format =
       openmeta::TransferTargetFormat::Tiff;
   exec_options.edit_target_path = "rendered.tif";

   openmeta::ExecutePreparedTransferFileResult exec =
       openmeta::execute_prepared_transfer_file("source.jpg", exec_options);

   openmeta::PersistPreparedTransferFileOptions persist;
   persist.output_path = "rendered_with_meta.tif";
   persist.overwrite_output = true;

   openmeta::PersistPreparedTransferFileResult saved =
       openmeta::persist_prepared_transfer_file_result(exec, persist);

Read once, save later
~~~~~~~~~~~~~~~~~~~~~

If your host already decoded source metadata during the initial load, keep a
decoded source snapshot and execute the later save without reopening the
source file.

.. code-block:: cpp

   #include "openmeta/metadata_transfer.h"

   openmeta::ReadTransferSourceSnapshotFileResult snapshot =
       openmeta::read_transfer_source_snapshot_file("source.jpg");

   openmeta::ExecutePreparedTransferSnapshotOptions options;
   options.prepare.target_format = openmeta::TransferTargetFormat::Tiff;
   options.edit_target_path      = "target.tif";
   options.execute.edit_apply    = true;

   openmeta::ExecutePreparedTransferFileResult result =
       openmeta::execute_prepared_transfer_snapshot(
           snapshot.snapshot, options);

Python mirrors that same host-facing snapshot flow:

.. code-block:: python

   from pathlib import Path

   import openmeta

   snapshot_info = openmeta.read_transfer_source_snapshot_file("source.jpg")
   snapshot = snapshot_info["snapshot"]

   result = openmeta.transfer_snapshot_file(
       snapshot,
       target_format=openmeta.TransferTargetFormat.Tiff,
       edit_target_path="target.tif",
       target_bytes=Path("target.tif").read_bytes(),
       output_path="edited.tif",
   )

Current source snapshots are decoded-store-backed by default. They are intended
for the normal EXIF/XMP/ICC/IPTC transfer flow, where OpenMeta re-emits decoded
metadata after applying the selected safety policy. If a host needs source
carrier provenance for diagnostics or a later passthrough policy decision,
enable ``ReadTransferSourceSnapshotFileOptions::preserve_raw_carriers`` or pass
``ReadTransferSourceSnapshotOptions`` with ``preserve_raw_carriers`` set.
Each raw carrier records its route, semantic kind, payload bytes, and
snapshot-local decoded entry ids attributed to that carrier.
Call ``raw_carrier_passthrough_audit_from_snapshot(...)`` before any host-owned
passthrough decision. The audit reports candidate carriers and primary block
reasons such as missing payload, target incompatibility, safety filtering,
content-bound C2PA, explicit profile policy, missing decoded entry links, or
unsupported carrier kind.
Python exposes the same check as
``snapshot.raw_carrier_passthrough_audit(...)``.
Snapshot preparation defaults to decoded re-emission. Hosts that need bounded
raw reuse can set ``PrepareTransferRequest::raw_carrier_passthrough_mode`` to
``TransferRawCarrierPassthroughMode::WhenSafe``, or pass
``raw_carrier_passthrough_mode=openmeta.TransferRawCarrierPassthroughMode.WhenSafe``
to Python snapshot transfer helpers. The current passthrough path is limited
to eligible non-C2PA JUMBF and OpenMeta draft unsigned C2PA invalidation
carriers for JPEG, JXL, and BMFF targets, plus draft unsigned C2PA
invalidation carriers for WebP. EXIF/XMP/ICC/IPTC remain decoded re-emitted.
If the host still owns the bundle/execution split, the lower-level
``prepare_metadata_for_target_snapshot(...)`` entry point remains available.
If the host already has a decoded ``MetaStore``, build a reusable snapshot with
``build_transfer_source_snapshot(store)``. If it already owns the source bytes
in memory, use ``read_transfer_source_snapshot_bytes(bytes)`` instead of the
file-path reader.
In Python, a previously decoded ``Document`` can be turned into a reusable
snapshot through ``doc.build_transfer_source_snapshot()`` or
``openmeta.build_transfer_source_snapshot(doc)``.
If it also owns the destination bytes in memory, call the overload
``execute_prepared_transfer_snapshot(snapshot, target_bytes, options)``.
If it already holds a prepared bundle, use
``execute_prepared_transfer_bundle(bundle, target_bytes, options)`` instead.
Snapshots can be persisted without retaining the original source or bundle:

.. code-block:: cpp

   std::vector<std::byte> persisted;
   openmeta::serialize_transfer_source_snapshot(snapshot.snapshot, &persisted);

   openmeta::TransferSourceSnapshot restored;
   openmeta::deserialize_transfer_source_snapshot(persisted, &restored);

Parsing is transactional and bounded by ``TransferSourceSnapshotIoOptions``.
Preserved raw carriers remain provenance data and are not implicitly safe to
relocate or rewrite.
Snapshot execution supports the same existing-sidecar merge and destination
carrier-precedence controls as the file helper; when loading an existing
sidecar it defaults to ``edit_target_path`` unless
``xmp_existing_sidecar_base_path`` is set explicitly.
For embedded-only writeback with sidecar cleanup and no filesystem path, set
``xmp_existing_destination_sidecar_state`` explicitly so OpenMeta can return a
cleanup decision without guessing a sidecar location.
Python now exposes those same split path/state controls directly:
``xmp_existing_sidecar_base_path``, ``xmp_sidecar_base_path``,
``xmp_existing_destination_embedded_path``, and
``xmp_existing_destination_sidecar_state``.

The CLI and Python command-line wrapper do not implement separate transfer
semantics. They map flags onto the same file-helper contract:

- ``--output`` is the sidecar base for ``sidecar`` and
  ``embedded_and_sidecar`` writeback, so generated sidecars use
  ``output-stem.xmp``.
- ``--xmp-writeback sidecar`` suppresses generated embedded XMP.
- ``--xmp-writeback embedded_and_sidecar`` writes generated XMP to both the
  edited output and generated sidecar.
- embedded-only writeback preserves an existing destination sidecar unless
  ``--xmp-destination-sidecar strip_existing`` is selected.
- sidecar-only writeback preserves existing destination embedded XMP unless
  ``--xmp-destination-embedded strip_existing`` is selected.
- ``--force`` maps to the C++ persistence overwrite flags for the primary
  output and generated sidecar.

Query runtime capabilities
--------------------------

Hosts can ask OpenMeta what the current build supports before wiring format
menus, warnings, or integration feature flags.

.. code-block:: cpp

   #include "openmeta/metadata_capabilities.h"

   openmeta::MetadataCapability cap = openmeta::metadata_capability(
       openmeta::TransferTargetFormat::Avif,
       openmeta::MetadataCapabilityFamily::Xmp);

   if (openmeta::metadata_capability_available(cap.target_edit)) {
       // The current build can edit AVIF XMP within the reported support level.
   }

Each operation reports one of ``unsupported``, ``supported``, ``bounded``, or
``disabled``. ``bounded`` means the capability exists within OpenMeta's
documented contract, not that it is arbitrary metadata-editor parity.
``disabled`` is used for compile-time-disabled support such as XMP decode when
XML support is not available.

Python exposes the same query:

.. code-block:: python

   cap = openmeta.metadata_capability(
       openmeta.TransferTargetFormat.Avif,
       openmeta.MetadataCapabilityFamily.Xmp,
   )
   print(cap["target_edit_name"])

Optional Adobe DNG SDK bridge
-----------------------------

If OpenMeta was built with ``OPENMETA_WITH_DNG_SDK_ADAPTER=ON``, you can use
the optional SDK bridge in two ways.

Update an existing DNG file
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: cpp

   #include "openmeta/dng_sdk_adapter.h"

   openmeta::ApplyDngSdkMetadataFileResult result =
       openmeta::update_dng_sdk_file_from_file("source.jpg", "target.dng");

Apply onto existing SDK objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: cpp

   #include "openmeta/dng_sdk_adapter.h"
   #include "openmeta/metadata_transfer.h"

   openmeta::PrepareTransferFileOptions prepare;
   prepare.prepare.target_format = openmeta::TransferTargetFormat::Dng;

   openmeta::PrepareTransferFileResult prepared =
       openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

   openmeta::DngSdkAdapterOptions adapter;
   openmeta::apply_prepared_dng_sdk_metadata(
       prepared.bundle, host, negative, adapter);

This bridge is for applications that already use the Adobe DNG SDK. OpenMeta
still does not encode pixels or invent raw-image structure.

Host-Owned Image Specs
~~~~~~~~~~~~~~~~~~~~~~

If a transfer target is produced from a different image buffer than the source,
the host writer owns the target image facts: dimensions, channel count, sample
type, compression, orientation, colorspace, ICC profile, and TIFF strip/tile
storage. OpenMeta does not infer those values from copied metadata. During
prepared transfer it filters source EXIF/XMP image-layout fields so stale source
properties are not written into a different output image.

Host code that encodes pixels should keep those fields from the target
container or inject values derived from the actual output buffer. Enable source
ICC transfer only when the host has verified that the profile matches the target
pixel buffer; otherwise preserve or write the target profile.

Use ``TransferProfile::safety`` for the broad source/destination relationship:

.. list-table::
   :header-rows: 1
   :widths: 18 32 50

   * - Mode
     - Use when
     - Transfer policy
   * - ``CompatibleFile``
     - Metadata is repackaged or recompressed into a compatible file/pixel representation
     - Preserve source camera, color, ICC, and camera-specific data except target-owned image-layout fields
   * - ``RenderedImage``
     - Pixels may have changed, especially RAW-to-JPEG/PNG/WebP/JXL/HEIF/AVIF export
     - Keep general/time/GPS/IPTC/portable XMP; drop source raw color
       calibration, profile/gain tables, raw digests/storage identifiers,
       linearization/crop/correction metadata, vendor RAW/source-processing
       geometry/color/correction/thermal/computational/private/stitch fields,
       camera raw settings XMP, source ICC, opaque MakerNotes, and non-C2PA
       JUMBF

See :ref:`transfer-safety-matrix` for the detailed per-group transfer matrix.

.. code-block:: cpp

   openmeta::PrepareTransferRequest request;
   request.target_format = openmeta::TransferTargetFormat::Jpeg;
   request.profile.safety = openmeta::TransferSafetyMode::RenderedImage;

   request.target_image_spec.has_dimensions = true;
   request.target_image_spec.width = encoded_width;
   request.target_image_spec.height = encoded_height;

   request.target_image_spec.has_samples_per_pixel = true;
   request.target_image_spec.samples_per_pixel = 3;
   request.target_image_spec.bits_per_sample_count = 1;
   request.target_image_spec.bits_per_sample[0] = 8;
   request.target_image_spec.has_photometric_interpretation = true;
   request.target_image_spec.photometric_interpretation = 2; // RGB
   request.target_image_spec.has_exif_color_space = true;
   request.target_image_spec.exif_color_space = 1; // sRGB

Python exposes the same structure as ``openmeta.TransferTargetImageSpec`` and
the command-line wrappers pass it through without a separate policy layer:

.. code-block:: python

   spec = openmeta.TransferTargetImageSpec()
   spec.has_dimensions = True
   spec.width = encoded_width
   spec.height = encoded_height
   spec.has_samples_per_pixel = True
   spec.samples_per_pixel = 3
   spec.bits_per_sample = [8]
   spec.has_photometric_interpretation = True
   spec.photometric_interpretation = 2
   spec.has_exif_color_space = True
   spec.exif_color_space = 1

For smoke testing the file-helper path, ``metatransfer`` and
``python -m openmeta.python.metatransfer`` expose equivalent flags:

.. code-block:: bash

   metatransfer --target-jpeg target.jpg -o output.jpg \
     --target-width 320 --target-height 240 \
     --target-samples-per-pixel 3 --target-bits-per-sample 8 \
     --target-photometric 2 --target-exif-color-space 1 \
     source.jpg

Phase One RAW Processing Metadata
---------------------------------

After decoding MakerNotes, hosts can query Phase One/Leaf RAW processing data
without depending on private MakerNote tag layout. The helper reports presence
and normalized values for color matrices, WB RGB levels, black level, sensor
temperatures, raw-data/storage byte counts, and sensor-calibration summaries.
These values are source-RAW processing metadata; do not write them into rendered
outputs unless the destination is a compatible RAW-style target.

.. code-block:: cpp

   #include "openmeta/phaseone_geometry.h"

   openmeta::PhaseOneRawGeometryResult geometry =
       openmeta::phaseone_raw_geometry_from_store(store);
   openmeta::PhaseOneRawProcessingResult raw =
       openmeta::phaseone_raw_processing_from_store(store);

   if (raw.status == openmeta::PhaseOneRawProcessingStatus::Ok &&
       raw.info.has_color_matrix1) {
       const double m00 = raw.info.color_matrix1[0];
       (void)m00;
   }

Python exposes the same normalized queries on decoded documents and reusable
transfer snapshots:

.. code-block:: python

   doc = openmeta.read("source.iiq", decode_makernote=True)
   geometry = doc.phaseone_raw_geometry()
   raw = doc.phaseone_raw_processing()

   if (raw["status"] == openmeta.PhaseOneRawProcessingStatus.Ok and
           raw["has_color_matrix1"]):
       m00 = raw["color_matrix1"][0]

The ``metaread`` command prints compact ``phaseone_raw_geometry=...`` and
``phaseone_raw_processing=...`` summaries when those decoded fields are present.

Vendor RAW Processing Metadata
------------------------------

For Sony, Canon, Nikon, Fujifilm, Pentax, Panasonic, Olympus, Kodak, Minolta,
Sigma, Samsung, Ricoh, Apple, DJI, Google, FLIR, Casio, Sanyo, KyoceraRaw,
Reconyx, HP, JVC, GE, Motorola, Nintendo, and Microsoft, OpenMeta exposes a
conservative grouped summary instead of vendor-specific decoded values. The
helper reports whether decoded MakerNote fields look like source RAW color/WB,
geometry/storage, lens correction, raw-data, sensor-calibration,
computational, thermal, preview/face geometry, stitch/panorama geometry, or
vendor-private table metadata. Use it to audit transfer safety decisions and
host UI, not as a rendered-output write source.
The same classification feeds semantic query and interpretation records as
per-family grouped table/vector candidates when multiple related vendor fields
are present.

.. code-block:: cpp

   #include "openmeta/vendor_raw_processing.h"

   openmeta::VendorRawProcessingSummary sony =
       openmeta::vendor_raw_processing_from_store(
           store, openmeta::VendorRawProcessingFamily::Sony);

   if (sony.fields_seen > 0) {
       const uint32_t unsafe_for_rendered = sony.color_fields +
           sony.white_balance_fields + sony.lens_correction_fields;
       (void)unsafe_for_rendered;
   }

   openmeta::TransferSafetyAudit audit =
       openmeta::transfer_safety_audit_from_store(
           store, openmeta::TransferSafetyMode::RenderedImage);

   if (audit.filtered_raw_color_calibration > 0 ||
       audit.filtered_icc_profiles > 0 ||
       audit.filtered_makernotes > 0) {
       // Show the host/user which source-bound metadata will not be transferred.
   }

   openmeta::TransferConceptDiagnostics diagnostics =
       openmeta::transfer_concept_diagnostics_from_store(
           store, openmeta::TransferSafetyMode::RenderedImage);

   openmeta::MetadataRawDataDescriptor raw_descriptor;
   raw_descriptor.encoding = openmeta::MetadataRawDataEncoding::Rendered;
   openmeta::TransferConceptDiagnostics descriptor_diagnostics =
       openmeta::transfer_concept_diagnostics_from_store(
           store, openmeta::TransferSafetyMode::CompatibleFile,
           raw_descriptor);

   for (size_t i = 0U; i < diagnostics.diagnostics.size(); ++i) {
       const openmeta::TransferConceptDiagnostic& item =
           diagnostics.diagnostics[i];
       const char* action =
           openmeta::transfer_concept_diagnostic_action_name(item.action);
       const char* reason =
           openmeta::transfer_concept_diagnostic_reason_name(item.reason);
       const char* severity =
           openmeta::transfer_concept_diagnostic_severity_name(item.severity);
       const char* message =
           openmeta::transfer_concept_diagnostic_message(item);
       const std::string token =
           openmeta::transfer_concept_diagnostic_token(item);
       const std::string message_token =
           openmeta::transfer_concept_diagnostic_message_token(item);
       const std::vector<std::string> message_arguments =
           openmeta::transfer_concept_diagnostic_message_arguments(item);
       (void)action;
       (void)reason;
       (void)severity;
       (void)message;
       (void)token;
       (void)message_token;
       (void)message_arguments;
   }

If a decoder exposes a curve/LUT metadata entry that only affects compressed RAW
storage, set ``raw_descriptor.requires_compressed_raw_encoding = true`` for the
descriptor-aware diagnostics call. OpenMeta will then treat that curve as not
applicable for uncompressed or packed raw buffers instead of assuming the
metadata is active.

If the decoder also knows that the curve/LUT only affects the primary raw
plane, set ``raw_descriptor.requires_primary_raw_plane = true`` and provide
``raw_descriptor.has_plane_index = true`` plus ``raw_descriptor.plane_index``
for the raw buffer being checked. Non-primary planes are then reported as not
applicable; unknown plane selection remains conditional.

Python uses the same family enum:

.. code-block:: python

   summary = doc.vendor_raw_processing(openmeta.VendorRawProcessingFamily.Nikon)
   if summary["fields_seen"]:
       print(summary["lens_correction_fields"])

   audit = doc.transfer_safety_audit(openmeta.TransferSafetyMode.RenderedImage)
   print(audit["filtered_raw_color_calibration"])

   diagnostics = doc.transfer_concept_diagnostics(
       openmeta.TransferSafetyMode.RenderedImage
   )
   raw_descriptor = openmeta.MetadataRawDataDescriptor()
   raw_descriptor.encoding = openmeta.MetadataRawDataEncoding.Rendered
   descriptor_diagnostics = doc.transfer_concept_diagnostics(
       openmeta.TransferSafetyMode.CompatibleFile,
       raw_descriptor,
   )
   for item in diagnostics["diagnostics"]:
       print(
           item["kind_name"],
           item["role_name"],
           item["action_name"],
           item["severity_name"],
           item["token"],
           item["message_token"],
           item["message_arguments"],
           item["message"],
       )

``metaread`` prints
``vendor_raw_processing[sony|canon|nikon|fujifilm|pentax|panasonic|olympus|kodak|minolta|sigma|samsung|ricoh|apple|dji|google|flir|casio|sanyo|kyoceraraw|reconyx|hp|jvc|ge|motorola|nintendo|microsoft]=...``
summaries when matching decoded fields are present.
Live-vendor source-processing coverage currently includes Apple computational
capture/HDR/motion fields, DJI pose and thermal fields, Google HDR+/shot-log
fields, and FLIR radiometric/raw-value/geometry fields. These buckets are used
by rendered-image safety filtering; they are not target-owned metadata for
rendered outputs.

Related pages
-------------

- :doc:`quick_start`
- :doc:`interop_api`
- :doc:`development`
- :doc:`api`
