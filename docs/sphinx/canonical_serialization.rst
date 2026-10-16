Canonical Metadata Serialization
================================

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

The current JPEG writer replaces the whole recognized Photoshop APP13 segment.
Other Photoshop resources sharing that segment with IPTC are not retained by
an IPTC rewrite. Canonical serialization does not change that carrier policy.
Resource-level APP13 merging remains open; hosts that require it must preserve
and wrap those resources themselves.

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
