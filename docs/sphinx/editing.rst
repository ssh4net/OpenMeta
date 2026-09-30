Metadata Editing
================

``openmeta/metadata_editing.h`` provides a bounded transactional editing
contract for the same logical fields accepted by Creation. It edits canonical
portable-XMP entries in a finalized ``MetaStore`` without requiring namespace
paths or entry IDs.

The API is experimental and versioned by
``kMetadataEditingContractVersion == 1``.

In 0.6.0, dirty native tombstones for IFD0 ImageDescription, Artist and Copyright
can reach TIFF/BigTIFF edit writers through the prepared bundle's explicit
removal list. Source omission still preserves destination entries. Fresh
emitters and codec handoffs reject deletion-bearing bundles. This changes the
bundle layout to ABI 4; see :doc:`migration_0_6` for the bounded contract.

Version 0.6.2 extends that list to Make, Model and Software. The existing
technical translator projects dirty ``tiff:Make``, ``tiff:Model`` and
``xmp:CreatorTool`` tombstones under ``ReplaceExisting``. Classic TIFF and
BigTIFF in both byte orders retain requests through snapshots, edit/stream and
package replay. Omission preserves existing native tags. Live native values
win over old tombstones; conflicting manual replacements fail before output.
ABI 4 and snapshot v1 stayed unchanged in 0.6.2. Version 0.7.0 adds grouped
timestamp deletion and synchronization across primary IFD0 and ExifIFD, with
explicit missing-companion intents and merging that preserves unselected
native destination entries. This changes the bundle to ABI 5; see
:doc:`migration_0_7`. Structural fields, other pages and private-record deletion
remain outside the bounded contract.

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

The combined fixture edits typed XMP, translates fifteen groups and checks
serialized snapshots through JPEG, classic TIFF and BigTIFF add/replace paths.

Structured Capture Update (0.5.8)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

OECF, SpatialFrequencyResponse, CFAPattern and DeviceSettingDescription add
four native targets in one transaction through
``translate_xmp_structured_capture_metadata`` and its Python counterpart.
The 0.5.8 combined inventory was 65 distinct native tags across fifteen APIs
(62 retained by compatible-file transfer). ABI 3 and host synchronization
remain unchanged; big-endian snapshots for these four tags need a 0.5.8
reader. See :doc:`translation` for the encoding, bounds and conflict contract.

EXIF Text Update (0.5.9)
------------------------

The 0.5.9 text/version batch adds UserComment, both version fields and seven
EXIF 3 text tags. The new EXIF text API also supports UTF-8 owner/lens fields.
The combined capture inventory is 75 distinct ExifIFD tags across sixteen APIs;
compatible-file transfer retains 72. Version and Artist/Software companion
requirements are explicit. ABI 3, snapshot v1 layout and host synchronization
responsibilities remain unchanged. BOM-less big-endian UserComment snapshots
require a 0.5.9 reader. See the EXIF text/version translation contract for limits
and the recorded OIIO/ExifTool reader limitations.

EXIF 3.1 Development and Correction Update (0.5.11)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A40D-A412 share the native schema, packed DevelopmentType resource,
``DevelopmentCharacterstic`` spelling, UTF-8 type-129 description and bounded
correction values used by the C++ and Python translation contract. Selected
writeback requires host EXIF version 0300 or 0310 and commits atomically.
Transfer retains present values, including rendered-image transfer; no RAW or
MakerNote processing history is inferred. See :doc:`translation`.

EXIF 3.1 LearningOptOutIn and Profile Authoring Update (0.5.12)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Typed editing and native validation share the bounded LearningOptOutIn and
profile translation contracts. LearningOptOutIn requires a complete ``exifEX``
usage/intention structure and an explicit EXIF 0300/0310 host policy. Profile
writeback covers ImageDescription, Artist, scalar Copyright, ColorSpace and
RelatedSoundFile; it does not infer ICC, image or audio authority or impose
complete-file companions. Failed validation, limits, conflicts and tombstones
remain transactional. See :doc:`translation`.
