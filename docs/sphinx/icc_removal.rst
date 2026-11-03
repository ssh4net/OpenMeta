Explicit primary ICC carrier removal
====================================

Version 0.8.28 appends ``remove_existing_icc`` to C++
``PrepareTransferRequest`` and ``PreparedTransferBundle``, default false.
Omission, disabled ICC inclusion and last-tag deletion never request removal.

Request and scope
-----------------

.. code-block:: cpp

   openmeta::PrepareTransferRequest request;
   request.target_format = openmeta::TransferTargetFormat::Jpeg;
   request.remove_existing_icc = true;
   // Prepare, then plan/apply against the existing target.

Explicit removal suppresses source ICC emission even when ``include_icc_app2``
is true. The source and snapshots remain unchanged. Other source metadata
retains its selected transfer policies. Check preparation status before using
its bundle; legacy preparation can publish a diagnostic bundle on failure.

The supported scope is leading JPEG ``ICC_PROFILE`` APP2 segments and primary
TIFF/BigTIFF/DNG IFD0 tag 34675. Image bytes and unrelated metadata remain.
Nested copies, JPEG data after SOS and DNG AsShot/Current profile tags are outside
the operation. TIFF removal changes active references; unreferenced historical
bytes may remain. An absent carrier can be a no-op: TIFF callers allow it with
``PlanTiffEditOptions::require_updates=false``. Other edits and existing C2PA
invalidation policies may still alter output.

Validation and consumers
------------------------

Unsupported target families fail preparation. Contradictory manually built
removal-plus-ICC-payload bundles fail validation. Fresh emit, adapter views,
payload-only packages, codec and stable handoffs reject this edit-only intent.
For fresh output without ICC, disable inclusion and leave removal false.

Existing exact edit packages can be materialized, serialized, parsed and replayed.
The package writer checks ICC absence in the virtual planned output after range
validation and before writer callbacks or publication. This rejects a full source
copy retaining ICC. The check allocates no complete output buffer or new replay
storage. JPEG leading-header work and TIFF IFD0 scan have a 4,096-item ceiling;
excess reports ``LimitExceeded``.

JPEG removal participates in existing C2PA invalidation and exact output binding.
Hosts still own cryptographic signing, trust and color-rendering decisions.

Compatibility
-------------

Use matching 0.8.28 headers and binaries within unfrozen ABI 4. Snapshot wire 1
and existing package, patch and ICC serialization contracts are unchanged.
The new flag is C++ only; existing Python operations keep their defaults.
See :doc:`icc_authoring`, :doc:`canonical_serialization` and :doc:`xmp_sync_policy`.
