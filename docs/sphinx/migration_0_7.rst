Migrating to OpenMeta 0.7
=========================

This guide describes the 0.7 release. For the current minimum-version
package policy, see :doc:`shared_library`.

OpenMeta 0.7.2 retains the **unfrozen development ABI label 4**. Public C++
layouts and APIs may change while the label stays 4. Rebuild applications and
plugins against matching release headers and library when they change; the
label alone does not establish compatibility with older ABI-4 binaries.
The initial local 0.7.0 build used label 5; 0.7.1 returns to 4 under this policy.
The current Windows runtime is ``openmeta-4.dll``; ELF/macOS use ABI major 4.

``PreparedTransferBundle`` carries ``tiff_exif_removals`` and
``tiff_merge_existing_exif``. Request ``find_package(OpenMeta 0.7 CONFIG REQUIRED)``.
``SameMinorVersion`` still rejects 0.6 requests. This discovery rule does not
freeze C++ layouts. Keep headers, objects and libraries from matching snapshots.

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

Capture and lens lifecycle (0.7.1)
----------------------------------

Capture, Flash, sensitivity and camera-text contracts are version 2. Accepted
ExposureTime, FNumber, base ISO, Flash, FocalLength and lens make/model/serial
owners create native Dirty+Deleted intent even when native keys are missing.
Accepted clean exact values receive same-value Dirty updates that preserve
bytes and wire provenance. Omission and conflict policies remain; intent and
update costs are checked before transactional publication and repeat calls reuse
intents. Hosts stage the separate calls before publishing an aggregate result.

Full sensitivity owns ``8827`` and ``8830``--``8835`` as one validated group;
optional absence becomes removal intent after complete validation. Basic ISO
owns only ``8827`` and preserves companions; disable it when full sensitivity
owns the source. Lens fields are independent. In 0.7.1, ExposureBiasValue,
LensSpecification and camera-owner/body-serial paths retained their prior behavior.
The 0.7.2 section below extends ExposureBiasValue.

TIFF preparation merges unspecified destination ExifIFD records for these dirty
edits. Bounded removal also accepts ``829A``, ``829D``, ``8827``, ``8830``--``8835``,
``9209``, ``920A`` and ``A433``--``A435``. MakerNote bytes and standard pointers
are preserved as opaque records; private relocation or checksum repair is not
provided. See :doc:`migration_0_7` for the unfrozen development ABI-4 policy.

TIFF edit requests
------------------

``tiff_ifd0_removals`` now also accepts IFD0 DateTime ``0132``. The new
``tiff_exif_removals`` list accepts primary ExifIFD ``9003``, ``9004``,
``9010``--``9012`` and ``9290``--``9292``. Both lists must be sorted and unique.
Preparation requires exact native IFD keys, Dirty+Deleted flags and enabled
EXIF output; live entries win over old tombstones. Snapshot v1 is unchanged.

Preparation enables ``tiff_merge_existing_exif`` for exact dirty native
timestamp and selected capture/lens members. This mode and per-tag ExifIFD removal preserve unspecified
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

APEX and capture-settings authority (0.7.2)
-------------------------------------------

APEX and capture-settings contracts are version 2; general capture is version 3
because its ExposureCompensation mapping shares the APEX bias owner. Each of
five APEX fields and twelve closed settings is independent. Accepted dirty
owner deletion carries exact native Dirty+Deleted intent even when native keys
are missing. Accepted clean exact values gain Dirty with original scalar and
wire provenance. Omitted or ineligible owners preserve destination values.

Existing conflict policies, exact APEX units, brightness unknown sentinel,
accepted enum codes, aliases and source shapes remain. Added intent, clean-marker
promotion and same-value authority updates consume preflight budgets. Repeat
calls reuse intent; each translator publishes once or leaves output unchanged.
Hosts stage separate calls before aggregate publication and disable one shared
ExposureBias mapping when composing APEX and general capture. No inference is
performed.

TIFF/BigTIFF uses the existing removal list and merge flag for the added tags:
``8822``, ``9201``--``9205``, ``9207``, ``A217``, ``A401``--``A403``,
``A406``--``A40A`` and ``A40C``. The allowlist now has 39 tags. Unselected native
records, opaque MakerNote bytes/original offsets, standard pointers and media
remain preserved. Snapshot v1, bundle/API layouts, ABI 4, package-minor policy
and unsupported-consumer rejection remain unchanged.
