Migrating to OpenMeta 0.7.0
===========================

OpenMeta 0.7.0 uses C++ ABI 5. ``PreparedTransferBundle`` adds
``tiff_exif_removals`` and ``tiff_merge_existing_exif``; rebuild applications
and plugins. The Windows runtime is ``openmeta-5.dll``; ELF/macOS use ABI
major 5. Request ``find_package(OpenMeta 0.7 CONFIG REQUIRED)``. The package's
``SameMinorVersion`` check rejects 0.6 requests. Keep older SDKs for consumers
that have not rebuilt; do not mix old headers or objects with ABI 5.

Timestamp authority
-------------------

The date and technical translation contracts are version 2. Accepted
``xmp:CreateDate``, XMP ``exif:DateTimeOriginal`` and ``xmp:ModifyDate`` owners
control their complete native timestamp family. Lexical timezone and fraction
control offset and subsecond presence. Missing companions create explicit
native dirty tombstones, even when their keys were absent from the source
store. A dirty owner tombstone removes all three members. Omission preserves
the destination. Existing conflict policies remain; accepted absent members
carry intent. Accepted clean exact timestamp members are marked dirty to retain
merge authority and count as updates against operation limits. Synthesized
entries count against added-entry and operation limits. Repeated translation reuses existing dirty tombstones. IPTC and
unrelated technical singleton behavior is unchanged.

Each translator is transactional. Hosts stage date and technical calls before
publishing a combined result. Fractional dates still require disabling lossy
IPTC mappings.

TIFF edit requests
------------------

``tiff_ifd0_removals`` now also accepts IFD0 DateTime ``0132``. The new
``tiff_exif_removals`` list accepts primary ExifIFD ``9003``, ``9004``,
``9010``--``9012`` and ``9290``--``9292``. Both lists must be sorted and unique.
Preparation requires exact native IFD keys, Dirty+Deleted flags and enabled
EXIF output; live entries win over old tombstones. Snapshot v1 is unchanged.

Preparation enables ``tiff_merge_existing_exif`` for exact dirty native
timestamp members. This mode and per-tag ExifIFD removal preserve unspecified
destination entries, including unselected timestamp families, capture fields,
standard pointers and opaque MakerNote bytes. Private offset/checksum repair
is outside this contract. Existing replacement behavior remains when
this mode is off. A present-empty source ExifIFD still requests whole-directory
clear; combining it with merge or ExifIFD removal is invalid. Source filtering
does not redact preexisting destination records in merge mode.

TIFF/BigTIFF edit, stream and package paths consume both lists. Invalid tags,
ordering, duplicates and replacement/removal conflicts fail before output.
Fresh emitters, codec handoffs and the DNG SDK adapter reject either nonempty
list. Use TIFF editing, including an empty scaffold when creating a fresh file
with removal intents. Manual native lists do not implicitly synchronize XMP.
Keep input and bundle unchanged between planning and applying, and replan
following changes. Python's path-based probe reports both lists and the flag.

Host-owned synchronization, existing patch/handoff contracts and snapshot
encoding stay unchanged. No general multi-page deletion, complete-file profile
or codec conformance is implied. See :doc:`editing` and :doc:`translation`.
