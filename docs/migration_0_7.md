# Migrating to OpenMeta 0.7

OpenMeta 0.7.1 retains the **unfrozen development ABI label 4**. Public C++
layouts and APIs may change while that label stays 4. Rebuild applications and
plugins against matching release headers and library whenever they change;
the label alone does not establish compatibility with older ABI-4 binaries.
The initial local 0.7.0 build used label 5; 0.7.1 returns to 4 under this policy.
The current Windows runtime is `openmeta-4.dll`; ELF/macOS use ABI major 4.

`PreparedTransferBundle` carries `tiff_exif_removals` and
`tiff_merge_existing_exif`. The installed package still uses `SameMinorVersion`:
request `find_package(OpenMeta 0.7 CONFIG REQUIRED)`. A 0.6 request does not
accept the 0.7 package. This package discovery rule does not freeze C++ layouts.
Do not mix headers, objects or libraries from different development snapshots.

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

## Capture and lens authority

Capture, Flash, sensitivity and camera-text translation contracts are version 2.
Accepted ExposureTime, FNumber, base ISO, Flash, FocalLength and lens make/model/
serial owners carry native deletion intent even when their source native keys
are missing. Accepted clean exact native values gain Dirty with a same-value
update that retains their bytes and wire provenance. Omission and existing
conflict policies remain. Added intents and authority updates count against
entry/operation limits before publication; repeated calls reuse them.

Full sensitivity owns PhotographicSensitivity and tags `8830`–`8835` as one
validated group. Optional absent members carry deletion intent only after the
complete source passes validation. Basic ISO owns only `8827` and preserves
sensitivity companions; disable its mapping when the full group owns that source.
Lens make/model/serial are independent singletons. ExposureBiasValue,
LensSpecification, camera-owner/body-serial and other capture translators retain
their existing behavior. Hosts stage the separate translator calls before
publishing an aggregate result.

TIFF preparation enables preservation of unspecified destination ExifIFD fields
for these dirty native edits. In addition to timestamp tags, bounded ExifIFD
removal accepts ExposureTime `829A`, FNumber `829D`, PhotographicSensitivity
`8827`, SensitivityType and extended sensitivity `8830`–`8835`, Flash `9209`,
FocalLength `920A`, LensMake `A433`, LensModel `A434` and LensSerialNumber `A435`.
Snapshot v1 remains unchanged.

## TIFF edit requests

The sorted unique `tiff_ifd0_removals` list adds DateTime `0x0132` to the six
previously supported root tags. The new sorted unique `tiff_exif_removals`
list accepts the capture/lens tags above and these primary ExifIFD timestamps:

| Family | Base timestamp | Offset | Subsecond |
| --- | --- | --- | --- |
| Modified | IFD0 `0x0132` | `0x9010` | `0x9290` |
| Original | `0x9003` | `0x9011` | `0x9291` |
| Digitized | `0x9004` | `0x9012` | `0x9292` |

Preparation collects exact native `ifd0`/`exififd` dirty tombstones with EXIF
output enabled. Live same-key entries win over old tombstones. Snapshot v1
already retains these flags, so its encoding is unchanged.

Preparation enables `tiff_merge_existing_exif` for exact dirty native timestamp
and selected capture/lens members. That explicit mode, and any ExifIFD per-tag removal, retains unspecified
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
