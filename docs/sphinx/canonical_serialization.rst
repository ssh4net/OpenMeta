Canonical Metadata Serialization
================================

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

Validation and output bounds are enabled by default. Opaque MakerNotes are
dropped unless explicitly preserved, and preservation does not reconstruct or
relocate vendor-private tables. The host must still ensure that image-dependent
metadata describes the actual encoded pixels.

See the complete contract in ``docs/canonical_serialization.md``.
