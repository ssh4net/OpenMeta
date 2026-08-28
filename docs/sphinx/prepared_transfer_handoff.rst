Prepared Transfer Handoff
=========================

Prepared Transfer Handoff v1 is the stable target-preparation boundary for
applications that own an encoder, writer, SDK object, or realtime transcoding
pipeline. It prepares target-specific metadata once and exposes immutable,
typed operations without exposing ``PreparedTransferBundle``, route strings,
or transfer internals.

Runtime contract
----------------

.. code-block:: cpp

   #include "openmeta/prepared_transfer_handoff.h"

   if (openmeta::prepared_transfer_handoff_contract_version()
       != openmeta::kPreparedTransferHandoffContractVersion) {
       // Reject an incompatible linked OpenMeta library.
   }
   if (openmeta::prepared_transfer_handoff_instance_contract_version()
       != openmeta::kPreparedTransferHandoffInstanceContractVersion) {
       // Disable the mutable per-worker path.
   }

``PreparedTransferHandoff`` is move-only and has a fixed opaque-pointer public
layout. The linked OpenMeta library owns and destroys all prepared state.

Prepare once
------------

``prepare_transfer_handoff(...)`` accepts a stable decoded
``TransferSourceSnapshot`` and a target request. It copies the required decoded
state, applies transfer policy, packages payloads, compiles typed operations,
and classifies metadata families. All allocation and route interpretation occur
during preparation. Failure is transactional and leaves an existing handoff
unchanged.

Stable v1 requires ``TransferRawCarrierPassthroughMode::Disabled``. Experimental
raw-carrier reuse is rejected rather than silently changing stable behavior.

Typed operation views
---------------------

``prepared_transfer_handoff_operation(...)`` returns a borrowed view containing
the semantic family, stable ``PreparedTransferAdapterOp``, and prepared payload.
EXR operations additionally expose explicit attribute name, type, value, and
opacity fields. Hosts never parse route strings. ``block_index`` is an opaque
correlation value, not an index into host-visible storage.

Views remain valid until the handoff is successfully prepared again, reset,
moved from, or destroyed.

Allocation-free replay
----------------------

``replay_prepared_transfer_handoff(...)`` invokes a ``noexcept`` function
pointer once per operation. Successful replay performs no allocation, route
parsing, vector construction, validation rebuild, locking, or hidden retry.
The same immutable handoff may be replayed repeatedly or concurrently with
independent callback state.

Per-worker mutable instances
----------------------------

Use ``PreparedTransferHandoffInstance`` when fixed-width time fields change
for each encoded image. Prepare one immutable template, create one independently
owned instance per worker, then patch and replay that worker's instance:

.. code-block:: cpp

   openmeta::PreparedTransferHandoffInstance worker;
   openmeta::PreparedTransferHandoffResult created =
       openmeta::create_prepared_transfer_handoff_instance(handoff, &worker);

   openmeta::PreparedTransferHandoffTimePatchFieldView field;
   const openmeta::PreparedTransferHandoffPatchResult described =
       openmeta::prepared_transfer_handoff_instance_time_patch_field(
           worker, openmeta::TimePatchField::DateTime, &field);

   constexpr char encoded_time[] = "2030:12:31 23:59:59";
   static_assert(sizeof(encoded_time) == 20U); // EXIF ASCII including NUL
   const openmeta::TimePatchView patch {
       openmeta::TimePatchField::DateTime,
       std::as_bytes(
           std::span<const char>(encoded_time, sizeof(encoded_time)))
   };
   const std::array<openmeta::TimePatchView, 1> patches = { patch };

   if (created.ok() && described.ok() && field.width == sizeof(encoded_time)
       && openmeta::patch_prepared_transfer_handoff_instance(&worker, patches)
              .ok()) {
       openmeta::replay_prepared_transfer_handoff_instance(
           worker, emit_to_codec, codec);
   }

Instance creation may allocate. It copies one contiguous payload buffer and
compact operation, semantic, EXR-view, block-range, and patch-slot state. It
does not copy routes, policy diagnostics, or generated sidecars and does not
recompile operations. The instance owns its state and remains valid after the
template is reset, moved, prepared again, or destroyed.

``prepared_transfer_handoff_instance_time_patch_field(...)`` reports the exact
serialized width and matching slot count without exposing payload offsets. A
generic host can therefore size source-dependent ``SubSec*`` and other patch
buffers before the hot path. Absent fields and non-uniform or invalid slot
layouts are reported before mutation.

Patching and replay allocate nothing. Every field must be valid, unique, and
present; serialized values must exactly match every corresponding slot width.
EXIF ASCII date/time fields normally include the terminating NUL. Aliased
instance payload input is rejected. Complete prevalidation makes every patch
failure transactional.

Each worker must own a separate instance. Independent instances may patch and
replay concurrently. One instance requires exclusive ownership during patch;
do not access or replay it concurrently with patch, reset, move, or destruction.
A borrowed operation view retains its address, but payload contents may change
after a successful patch.

Target coverage
---------------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Target
     - Typed operation
   * - JPEG
     - Marker code and payload
   * - TIFF, DNG
     - TIFF tag and payload
   * - JPEG XL
     - Box or ICC-profile operation
   * - WebP
     - RIFF chunk type and payload
   * - PNG
     - Chunk type and payload
   * - JP2
     - Box type and payload
   * - HEIF, AVIF, CR3
     - BMFF item/property fields and payload
   * - EXR
     - Attribute name, type, value, and opacity

Format and metadata-family capabilities remain separate. Query
``metadata_capability(...)`` before assuming that a requested family can be
serialized into a target.

Deliberate boundary
-------------------

Stable Handoff v1 excludes source reading, snapshot persistence, raw-carrier
passthrough, direct bundle/block/route access, variable-width or structural
metadata mutation, prepared artifact persistence, destination-byte editing,
and encoder object ownership. Those APIs remain available where documented
but are still experimental.
