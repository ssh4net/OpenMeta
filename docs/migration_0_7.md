# Migrating to OpenMeta 0.7.0

OpenMeta 0.7.0 uses C++ ABI 5. `PreparedTransferBundle` adds
`tiff_exif_removals` and `tiff_merge_existing_exif`; rebuild applications and
plugins against the new headers and library. The Windows runtime is
`openmeta-5.dll`; ELF/macOS use ABI major 5. The installed package uses
`SameMinorVersion`: request `find_package(OpenMeta 0.7 CONFIG REQUIRED)`.
A 0.6 request does not accept this package. Keep older SDKs for consumers
that have not rebuilt, and do not mix their objects or headers with ABI 5.

## Timestamp authority

The date and technical translation contracts are version 2. An accepted
`xmp:CreateDate`, XMP `exif:DateTimeOriginal`, or `xmp:ModifyDate` owns its
complete native EXIF timestamp family. The lexical timezone and fraction
control the corresponding offset and subsecond tags. A value without them
produces explicit native dirty tombstones, including when the native keys
were absent from the translation input. A dirty owner tombstone removes the
base timestamp and both companions. Source omission or an ineligible source
property preserves the destination.

`PreserveExisting` retains the whole group when any native member is active.
`FailOnConflict` requires absence or exact equivalence; `ReplaceExisting`
reconciles the group. Accepted absent members carry removal intents under any
of these policies. Accepted clean, exact timestamp members are marked dirty so preparation
retains the merge authority; these same-value updates count against the
operation limit.
Synthesized intents count against `max_added_entries` and
`max_operations`, and repeated translation reuses existing dirty tombstones.
IPTC and other technical singleton behavior remains unchanged.

Each translator is transactional. Creation-date and technical translation are
separate calls; hosts stage their results and publish only after every required
call succeeds. Fractional dates still require disabling lossy IPTC mappings.

## TIFF edit requests

The sorted unique `tiff_ifd0_removals` list adds DateTime `0x0132` to the six
previously supported root tags. The new sorted unique `tiff_exif_removals`
list accepts only these primary ExifIFD tags:

| Family | Base timestamp | Offset | Subsecond |
| --- | --- | --- | --- |
| Modified | IFD0 `0x0132` | `0x9010` | `0x9290` |
| Original | `0x9003` | `0x9011` | `0x9291` |
| Digitized | `0x9004` | `0x9012` | `0x9292` |

Preparation collects exact native `ifd0`/`exififd` dirty tombstones with EXIF
output enabled. Live same-key entries win over old tombstones. Snapshot v1
already retains these flags, so its encoding is unchanged.

Preparation enables `tiff_merge_existing_exif` for exact dirty native timestamp
members. That explicit mode, and any ExifIFD per-tag removal, retains unspecified
destination ExifIFD entries while applying replacements and removals. Unselected
timestamp families, capture fields, standard pointers and opaque MakerNote
bytes survive. Private offset/checksum repair is outside this contract.
Outside this mode, the existing ExifIFD replacement behavior remains. A
present-empty source ExifIFD still requests whole-directory clearing; combining
that clear with merge or ExifIFD removals is invalid. Merge mode preserves
existing destination records; source transfer filtering does not redact them.

TIFF/BigTIFF edit plan/apply, streams and edit packages consume both lists.
Unsupported tags, unsorted/duplicate lists and simultaneous replacement/removal
fail before output or callbacks. Fresh emitters, codec handoffs and the DNG SDK
adapter reject either nonempty removal list. Use TIFF editing when carrying
removal intents, including editing a fresh empty TIFF scaffold. A manual native
removal list does not implicitly translate or synchronize XMP.

Keep the input and prepared bundle unchanged between planning and applying,
and replan after any change. The Python transfer probe reports both removal
lists and the merge flag; it remains a path-based diagnostic API. Preparation
and planning may allocate. Host-owned synchronization and existing patch,
handoff and snapshot contracts remain unchanged. No general multi-page TIFF
deletion, complete-file profile or codec conformance is implied.
