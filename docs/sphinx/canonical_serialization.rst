Canonical Metadata Serialization
================================

ICC authoring and serialization (0.8.25)
----------------------------------------

ICC authoring/editing contracts 2 and canonical ICC serialization contract 1
support fresh host-provided header/tag data, transactional exact-key changes,
checked measure/write, deterministic layout and existing ICC carrier paths.
Profile IDs clear by default; dirty deletion intent is retained before transfer
filtering. Clean transfer compatibility remains, with corrected fixed-point
illuminant encoding. ABI 4 and existing layouts are retained. Full RDF authoring
remains open; ICC class/tag/color semantics stay host-owned.
See :doc:`icc_authoring` for the complete contract.

Prepared native IPTC patching (0.8.24)
--------------------------------------

Prepared patch contract 2 adds native IPTC-IIM beside EXIF and scalar XMP in one
transactional plan. Select an active dataset occurrence and exact native shape;
the canonical serializer records value offsets by source entry ID. Fixed-width
updates preserve dataset ordering, duplicate identity and short/extended headers.
Text, bytes and well-formed arrays are copied raw; no implicit transcoding,
terminator insertion or numeric endian conversion occurs. Source whole-store
validation remains independent of raw IPTC update semantics.

Preparation and worker creation may allocate. Patch success/failure, payload
views and replay remain allocation-free, with stable payload addresses and sizes.
Every family is preflighted before any write; alias checks cover all payloads.
Replay preserves EXIF then XMP order and appends IPTC. Hosts own synchronization,
framing and publication. The native IIM limits are explicit in ``options.iptc``;
new widths or fields require preparing a new plan. No Python patch binding is
added. See ``docs/canonical_patching.md`` for the full contract.

Version 0.8.24 retains unfrozen ABI 4 and appends IIM options/status to public
patch structures. Rebuild consumers with matching headers and binaries. Source
snapshot v1, IIM serializer contract 1, signer packages and Host Adoption Profile
v1 are unchanged. No atomics, mutexes or automatic cross-family synchronization
were added. Full RDF authoring remains open; bounded ICC construction is covered by 0.8.25 above.

Edited-snapshot C2PA signing (0.8.23)
-------------------------------------

``prepare_c2pa_signing_context()`` combines an edited ``TransferSourceSnapshot``
with the exact target bytes for JPEG, boxed JPEG XL, HEIF, AVIF and CR3. The
owned ``PreparedTransferC2paSigningContext`` retains the prepared metadata,
external-signer handoff and complete rewritten asset stream without C2PA.
Metadata preparation and content-binding results remain separate. In particular,
C2PA-only input can return ``Unsupported/RequestedMetadataNotSerializable`` for
metadata preparation and ``Ok`` for binding; check both results.

Pass the handoff to the host's manifest builder and signer, then use
``execute_prepared_c2pa_signing_context()`` with the signer material and target
bytes. Validation replans the target and compares the complete binding stream
before staging or publishing output. A changed scan/codestream byte or prepared
metadata value fails. Changes confined to bytes removed or replaced by the
rewrite can pass when the complete binding stream and request semantics match.
The executor uses that fresh plan. JPEG signing contexts require complete marker
framing, bounded segment/SOS headers and an EOI marker; trailing bytes are
retained. This structural check does not decode or validate image pixels.
Contexts and source snapshots remain unchanged.
The host provides signature creation, certificate trust and cryptographic
verification; OpenMeta checks the existing structural signer contract.

``C2paSigningContextLimits`` defaults to 200,000 snapshot entries and 64 MiB of
snapshot arena data. These two limits must be nonzero. ``max_binding_bytes=0``
allows any binding size; set a nonzero limit to bound its materialization.
``OpenMetaResourcePolicy::max_file_bytes`` independently caps target input bytes.
The decoded-store ceilings exclude separately owned raw carriers; bound
optional raw-carrier ingestion with the snapshot read/serialization limits.
These preparation, validation and execution operations allocate. Hosts must
synchronize access to shared mutable objects. Preflight failures publish no
context or output bytes; a byte writer's I/O failure retains the existing
partial-write behavior.

Python exposes ``prepare_c2pa_signing_context(snapshot, target_bytes, ...)``,
returning independent ``prepare_status``, ``binding`` and ``overall_status`` fields.
Set ``jumbf_policy=TransferPolicyAction.Drop`` to omit projected JUMBF
when requesting C2PA-only output. The returned ``C2paSigningContext`` provides ``request()``, ``validate(target_bytes)``
and ``execute(target_bytes, ..., apply=True)``. Pass immutable Python ``bytes`` for
the target. Raw handoff/binding bytes require ``unsafe_handoff_bytes()`` and
``unsafe_binding_bytes()``; output asset bytes require ``unsafe_execute()``.
These accessors export the retained stream and handoff issued at preparation;
they do not inspect another target. If the target changes before signing, call
``validate(current_target_bytes)`` before exporting. Execution always validates
the supplied target again. The safe executor reports results without exposing
asset bytes. Python accepts
``policy``, ``max_snapshot_entries``, ``max_snapshot_arena_bytes`` and
``max_binding_bytes`` with the C++ defaults.

ABI remains 4; rebuild with matching 0.8.23 headers and binaries. Existing public
layouts, source snapshot v1 and external-signer package wire formats are unchanged.
Older preparation entrypoints remain available; use this explicit context API
when the source is an edited snapshot and signing requires target bytes.

IPTC carrier removal (0.8.20)
-----------------------------

Prepared bundles now carry explicit ``remove_existing_iptc`` edit intent. Preparation
sets it for JPEG/TIFF/DNG only when IPTC output is enabled, no active native IPTC
dataset remains, and at least one native dataset is marked dirty and deleted.
Omission, clean tombstones and disabled IPTC output do not request removal.
All datasets count, including charset/version records; delete those explicitly
when clearing the complete carrier. No automatic pruning is performed.

JPEG edits remove managed IPTC resources from leading Photoshop APP13 streams,
preserving unrelated resources byte for byte. TIFF/BigTIFF/DNG edits remove IFD0
tag 33723 and preserve other tags and pixel data. This does not erase every
possible embedded copy. Removal conflicts with a prepared IPTC payload. Fresh
emission and payload-only handoffs reject the edit-only intent; exact-output
packages and complete C2PA binding requests carry the edited bytes. The explicit
0.8.23 signing-context API supports edited snapshots plus target bytes; the
older snapshot preparation entrypoint alone cannot establish that binding.
Snapshot wire v1 is unchanged.

The bundle layout changes within unfrozen ABI 4. Rebuild consumers with matching
0.8.20 headers and binaries. Preparation may allocate; hosts synchronize shared
objects. General transfer and existing handoff wire versions remain unchanged.

IPTC-IIM in 0.8.15
------------------

``openmeta/iptc_iim_serialize.h`` exposes ``serialize_iptc_iim()`` for active
IPTC datasets in a finalized store. Measure with an empty span, then supply
``needed`` bytes. Short spans receive a deterministic prefix and return
``OutputTruncated``; other errors leave output unchanged. Output must not overlap
the store arena; detected overlap returns ``InvalidOptions``.

Bytes, Text and well-formed Arrays are copied as raw payload bytes, including
empty values and unknown datasets. No numeric encoding, endian conversion,
text transcoding or charset-marker insertion occurs. Field-level semantics and
charset consistency are not validated. Duplicate datasets remain distinct.
CodedCharacterSet (1:90) sorts first, then source order, record, dataset and
original entry order for ties. Lengths above 32,767 use the four-byte extended
form. No Photoshop, JPEG or TIFF framing is included.

Default limits are 200,000 datasets, 8 MiB per dataset and 64 MiB output.
All limits must be nonzero; output cannot exceed ``UINT32_MAX`` or addressable
size. Invalid selected keys, unsupported values, malformed spans/counts and
Truncated/Unreadable entries reject before publication. ``failed_entry`` identifies
an offending entry when applicable. Measurement allocates no payload or ordering
scratch; writing may allocate bounded scratch and a complete temporary payload.
System allocation failure is not a recoverable status. Hosts synchronize mutations.

JPEG/TIFF dataset rebuilds use the same wire engine with best-effort skip warnings;
limits fail the whole rebuild. The unchanged opaque Photoshop fallback remains
when no native IPTC dataset is dirty. That pass-through is outside canonical
validation. Existing layouts, snapshot v1 and development ABI 4 remain unchanged;
new symbols require matching 0.8.15 headers and binaries. This API adds no prepared
IPTC patching. See ``docs/canonical_serialization.md`` for the example and contract.

JPEG IPTC replacement in 0.8.16
-------------------------------

Managed IPTC edits validate complete Photoshop ``8BIM`` resource streams and
replace all old ``0x0404`` records. Other records retain exact names, duplicate
IDs, data and padding. Each mixed source yields a separate residual APP13;
replacement segments are inserted once. Pure non-IPTC segments remain identical.
No IPTC update bypasses resource parsing. Post-SOS bytes remain opaque.
Preservation does not recalculate semantic dependencies such as IPTC digests.

Mixed resources and extra old IPTC segments require metadata rewrite. Matching
IPTC-only segment counts and sizes can use in-place editing. Malformed input or
replacement streams fail before apply/package publication or writer calls.
Mixed or IPTC-free managed replacements are unsupported. Sink errors can still
leave partial external output. Plans remain summaries; hosts keep input and
bundle immutable and synchronize mutations.

Output counters include residual segments; removed/replaced counters refer to
original markers. C2PA binding includes the same residual bytes through the new
``PreservedJpegPhotoshopResources`` kind. Packages with this kind use wire version
2; legacy kinds retain version 1. See ``docs/canonical_serialization.md`` for
descriptor fields, counter semantics and the signing-option boundary.

EXIF/TIFF
---------

``openmeta/exif_tiff_serialize.h`` exposes the deterministic target-neutral
EXIF/TIFF serializer used by transfer preparation.

Call ``serialize_exif_tiff(store, {})`` to obtain ``needed``, allocate that
many bytes, then call it again with the caller-owned span. Output is an
unwrapped little-endian TIFF stream beginning with ``II`` and TIFF magic 42.
It does not contain ``Exif\0\0`` or container framing.

Current wrappers are:

.. list-table::
   :header-rows: 1

   * - Destination
     - Prepared EXIF payload
   * - JPEG and current TIFF/DNG handoff
     - ``Exif\0\0`` plus TIFF
   * - PNG ``eXIf`` and WebP ``EXIF``
     - TIFF
   * - JP2/JXL/BMFF EXIF carrier
     - big-endian offset 6 plus ``Exif\0\0`` plus TIFF

Preparation may allocate. Serialize once and retain immutable bytes for an
allocation-free replay path. Repeated serialization against an immutable
finalized store is deterministic and thread-safe.

For repeated fixed-width changes, ``openmeta/metadata_patch.h`` compiles exact
key occurrences into opaque target-neutral handles. See
:doc:`canonical_patching`.

Validation and output bounds are enabled by default. Opaque MakerNotes are
dropped unless explicitly preserved, and preservation does not reconstruct or
relocate vendor-private tables. The host must still ensure that image-dependent
metadata describes the actual encoded pixels.

See the complete contract in ``docs/canonical_serialization.md``.
