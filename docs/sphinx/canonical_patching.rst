Prepared Canonical EXIF Patching
================================

``openmeta/exif_tiff_patch.h`` prepares immutable unwrapped TIFF/EXIF bytes and
compiles exact key occurrences into opaque fixed-width handles. It does not
select a destination container or expose TIFF byte offsets.

Preparation and worker creation may allocate. Create one
``PreparedExifTiffPatchInstance`` per execution lane. Transactional typed patch
batches and ``payload()`` access do not allocate or resize storage. Immutable
plans support concurrent const access; independent worker instances can be
patched and replayed concurrently.

The v1 contract supports source-backed scalar and array values emitted by the
classic TIFF serializer, signed and unsigned rationals, fixed byte payloads,
and fixed-width ASCII/UTF-8 text. Counts, logical types, and encodings cannot
change. Failed batches leave the complete worker payload unchanged and reject
foreign/duplicate handles, zero rational denominators, width changes, malformed
slots, and values that alias worker storage.

``U64`` and ``I64`` integer entries, regenerated IFD pointers, synthetic
fields, and variable-width changes are not patchable in the classic TIFF v1
contract. Hosts add their JPEG, PNG/WebP, JP2/JXL/BMFF, JPH, or private-container
framing after patching.

See the complete contract in ``docs/canonical_patching.md``.
