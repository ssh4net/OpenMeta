Metadata Editing
================

``openmeta/metadata_editing.h`` provides a bounded transactional editing
contract for the same logical fields accepted by Creation. It edits canonical
portable-XMP entries in a finalized ``MetaStore`` without requiring namespace
paths or entry IDs.

The API is experimental and versioned by
``kMetadataEditingContractVersion == 1``.

C++ example
-----------

.. code-block:: cpp

   #include "openmeta/metadata_editing.h"

   #include <array>

   const std::array operations = {
       openmeta::make_metadata_edit_set(
           openmeta::make_metadata_creation_text(
               openmeta::MetadataCreationFieldKind::Title, "Edited title")),
       openmeta::make_metadata_edit_add(
           openmeta::make_metadata_creation_text(
               openmeta::MetadataCreationFieldKind::Keyword, "approved")),
       openmeta::make_metadata_edit_remove(
           openmeta::MetadataCreationFieldKind::Creator, 0),
   };

   openmeta::MetadataEditingRequest request;
   request.operations = operations;

   openmeta::MetaStore edited;
   const openmeta::MetadataEditingResult result =
       openmeta::edit_metadata(source, request, &edited);

``source`` must be finalized. The output is replaced only after every
operation succeeds. On failure, the output remains unchanged and
``failed_operation_index`` identifies the rejected operation when available.

Operation semantics
-------------------

``Add`` creates an absent singleton or appends a creator/keyword value.
``Set`` replaces one active value while preserving its key, origin, block,
wire provenance, and existing flags, then adds ``Dirty``. ``Remove`` leaves a
``Deleted | Dirty`` tombstone. ``RemoveAll`` tombstones every active
occurrence.

Operations observe earlier operations in request order. Repeated creators and
keywords use a zero-based active occurrence. Removing one shifts later values
for subsequent operations. New repeated values receive the next unused
canonical property index; gaps are not renumbered.

Singleton fields accept occurrence zero only. Duplicate active singleton
values are ambiguous for single-value ``Set`` and ``Remove``. Use
``RemoveAll`` followed by ``Add`` to repair them deliberately. Missing targets
are errors rather than silent no-ops.

Provenance and blocks
---------------------

``Set`` changes only value and dirty state; ``Remove`` changes only flags.
``Add`` uses an existing valid XMP block when one is available. A finalized
empty store can also accept new metadata; those entries have no source block
and remain serializable by portable XMP and transfer preparation.

Tombstones are not compacted automatically. Use the lower-level ``compact``
helper only when losing deleted-entry identity is appropriate.

Validation and limits
---------------------

``Add`` and ``Set`` use the Creation mapping and validation. The hard limits
are 1024 operations, 1 MiB of text per value operation, and 8 MiB total text
per request. Caller limits may lower but not raise these bounds. Calls use no
global state and are safe with an immutable finalized source and distinct
output stores.

Python
------

.. code-block:: python

   import openmeta

   K = openmeta.MetadataCreationFieldKind
   edited = document.edit_metadata([
       openmeta.metadata_edit_set(
           openmeta.metadata_creation_text(K.Title, "Edited title")),
       openmeta.metadata_edit_add(
           openmeta.metadata_creation_text(K.Keyword, "approved")),
       openmeta.metadata_edit_remove(K.Creator, 0),
   ])

Python owns operation text and invokes the same C++ contract. The method
returns a detached edited ``Document`` and does not mutate the source. Invalid
requests raise ``ValueError`` with the C++ status and operation index.

Current scope
-------------

This milestone covers the 24 logical fields documented by Creation. It does
not yet provide high-level arbitrary wire/custom keys, non-default language
selection, structural block editing, full cross-family synchronization, or
direct in-place file patching. Supported edited creation dates can be projected
explicitly into native EXIF/IPTC groups before persistence; see
:doc:`translation`. Lower-level ``MetaEdit`` remains available for entry-ID-
based host code; transfer and writer APIs handle persistence.

Exact typed keys (0.5.6)
------------------------

``edit_metadata_typed`` uses borrowed ``MetadataAuthoringEntry`` keys and values
for EXIF/TIFF, IPTC-IIM and XMP, including private/custom entries. Inputs are
copied; every supplied value and the complete final candidate are validated
before publication. A failed batch leaves source and output unchanged, even
when they alias. This experimental v1 API is C++ only.

Add defaults to ``FailIfPresent``; explicit ``Append`` allows repeated keys,
subject to final schema singleton checks. Set and Remove default to
``kMetadataTypedEditingUniqueOccurrence``. Numeric occurrences select current
active exact-key matches. Remove also accepts ``kMetadataEditingAllOccurrences``.
Missing targets fail. Ordered Remove-all then Add can repair duplicates.

Set retains entry identity, block and order, replaces wire hints and clears
obsolete wire type names. Omitted hints request inference. Remove retains dirty
tombstones; Add has no original source block. Exact XMP indices are not
renumbered. There is no implicit alias translation or container restructuring.

Defaults bound requests to 4096 primitive operations, 8 MiB of key/payload
bytes, 200000 output entries, 64 MiB output arena/value bytes and 4096 bytes per
key component. Remove-all expansion counts toward the operation limit. Retained
tombstones/arena bytes and existing lower store ceilings count toward output
limits. Every supplied value must be valid even if later overwritten; selected
schema and complete candidate validation can reject unrelated malformed base
metadata. Preparation can allocate; the host synchronizes conflicting access.

The combined fixture edits typed XMP, translates twelve groups and checks
serialized snapshots through JPEG, classic TIFF and BigTIFF add/replace paths.
