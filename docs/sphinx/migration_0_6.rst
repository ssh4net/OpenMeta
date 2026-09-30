Migrating to OpenMeta 0.6.0
===========================

OpenMeta 0.6.0 adds ``PreparedTransferBundle::tiff_ifd0_removals`` and changes
the public bundle layout. Rebuild applications and plugins against ABI 4.
Do not mix 0.5 headers or objects with the 0.6 library. The Windows runtime
is ``openmeta-4.dll``; ELF/macOS use ABI major 4. CMake consumers should use
``find_package(OpenMeta 0.6 CONFIG REQUIRED)``. The package rejects 0.5 requests.

Explicit native TIFF deletion
-----------------------------

The first bounded contract covers IFD0 ImageDescription (``0x010E``), Artist
(``0x013B``) and Copyright (``0x8298``). Preparation collects exact native
``ifd0`` entries with both ``Dirty`` and ``Deleted`` flags when EXIF transfer
is enabled. A live entry wins over historical tombstones; duplicate tombstones
produce one sorted instruction. Source omission and clean tombstones preserve
destination tags. Snapshot v1 already preserves the required entry flags.

Version 0.6.2 adds IFD0 Make (``0x010F``), Model (``0x0110``) and Software
(``0x0131``) without changing ABI 4 or snapshot v1. These additions require
0.6.2 or newer; older 0.6 libraries reject their manual removal instructions.
Deleting these values does not delete separate camera or software records.

Manual removal lists must be sorted, unique and restricted to those tags.
Invalid lists and contradictory serialized replacements fail before output.
Existing TIFF/BigTIFF edit planning, apply, stream and edit-package paths honor
the instructions. Fresh tag emitters and typed codec handoffs reject deletion
requests before callbacks because they do not support destination deletion.

``ReplaceExisting`` translation edits the source store. A source-omitted field
does not become a destination removal. Preserve the explicit native tombstone
through preparation. Native deletion does not implicitly synchronize XMP;
its preparation and carrier policies remain separate. Structural tags,
MakerNotes, other IFDs and page directories remain outside this contract.

Preparation and planning may allocate. Host-owned synchronization remains
unchanged; no atomics, mutexes or complete-file conformance rules are added.
