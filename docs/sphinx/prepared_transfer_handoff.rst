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
passthrough, mutable time patches, direct bundle/block/route access, prepared
artifact persistence, destination-byte editing, and encoder object ownership.
Those APIs remain available where documented but are still experimental.
