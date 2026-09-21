# Standard EXIF authoring inventory

Audit date: 2026-09-17. Implementation baseline: **OpenMeta C++ 0.5.9**, ABI 3,
commit `6d04da1`. This inventory records current behavior and selects the next
batch. It does not add fields or change the library version.

**0.5.10 implementation update:** the validation batch selected below is now
implemented. All 64 routed fields have fixed schema entries; five registered
Interop fields and three structural pointers are also recognized. MeteringMode
type/value checks and legal Interop ID handling are covered by combined
authoring/editing/serialization regressions. See
[the validation contract](generic_authoring.md#validation). The counts and
runtime observations below retain the historical 0.5.9 audit baseline.

**0.5.11 implementation update:** the six EXIF 3.1 development/correction
fields A40D–A412 now have a shared transactional C++/Python translator, native
schemas, portable XMP and type-129 serialization. The published
`DevelopmentCharacterstic` spelling and the explicit 0300/0310 host policy are
preserved. LearningOptOutIn remains a separate structured contract.

## Scope and sources

The denominator is the primary-image data fields in CIPA Tables 6, 8, 9, 14
and 16. It excludes duplicate thumbnail instances, TIFF/EP and DNG extensions,
vendor fields and three structural pointers: ExifIFD `8769`, GPSIFD `8825`
and InteroperabilityIFD `A005`. IDs in this document are hexadecimal.

The pinned sources are [EXIF 3.0, corrected 2024 edition][exif30],
[EXIF 3.1, 2026 edition][exif31] and the [2026 XMP mapping specification][xmp31].
The table counts were checked against the registry, public API declarations,
the combined capture fixture and the fixed validation schema.

| Standard field group | Data fields | Explicit reverse targets in 0.5.9 | Boundary |
| --- | ---: | ---: | --- |
| TIFF/IFD0, Table 6 | 30 | 7 | Four technical fields and three geometry fields |
| ExifIFD, EXIF 3.0 Tables 8/9 | 88 | 85 | 75 capture, eight date and two geometry fields |
| ExifIFD, EXIF 3.1 Tables 8/9 | 95 | 85 | Adds seven fields to the preceding edition |
| GPSIFD, Table 14 | 32 | 31 | GPSVersionID is a managed companion |
| InteropIFD, Table 16 | 1 | 0 | InteroperabilityIndex; other registry entries are extensions |

The two ExifIFD rows describe overlapping editions and must not be added.
A route means an explicit bounded XMP-to-native API. It does not mean support
for every legal value, a complete EXIF file profile, or competitor parity.
Generic exact-key authoring and reading a registry name are separate capabilities.

The seventeen capture APIs have **86 mappings to 81 distinct tags**. Five tags
have two owners: ISO, ExposureBiasValue, CameraOwnerName, LensMake and LensModel;
the six development/correction fields are new singleton targets.
See [capture synchronization](capture_sync_milestone.md) for the complete list.

## Existing validation gaps

The fixed schema contains 17 of the 30 IFD0 fields, 45 of the 95 ExifIFD
fields, nine of the 32 GPS fields and no Table 16 Interop field. These are
schema-entry counts, not a measure of semantic validation completeness.
Among fields with explicit reverse routes, **64 lack a fixed schema entry**:
41 ExifIFD and 23 GPSIFD fields. All seven routed IFD0 fields have entries.

Two runtime reproductions show why this work precedes new writeback fields:

| Input to generic authoring | Observed 0.5.9 behavior | Gap |
| --- | --- | --- |
| Text at ExifIFD MeteringMode `9207` | Accepted with the default unknown-tag policy | A standard SHORT field is treated as unknown; its field type is not checked. |
| Valid SHORT MeteringMode with unknown tags set to error | Rejected as `UnknownExifTag` | Strict policy cannot recognize this existing reverse target. |
| ASCII `R98` at InteropIFD InteroperabilityIndex `0001` | Rejected as `WrongIfd` | Numeric ID `0001` is recognized in GPSIFD but has no Interop schema entry. |

These outcomes follow the schema lookup in
[`metadata_store_validate.cc`](../src/openmeta/metadata_store_validate.cc).
Legal tag identity includes its IFD. The current fallback classifies a tag
known only in another IFD as misplaced, which rejects this legal reused ID.

Other probes locate profile boundaries rather than establish new guarantees.
Generic authoring accepts ImageTitle without ExifVersion, Photographer without
Artist and reserved ColorSpace `42`. The EXIF text reverse API accepts
CameraOwnerName without Artist. The technical reverse API rejects UTF-8 Make
under its documented ASCII-only contract. Successful detached-store validation
therefore does not establish complete-file conformance.

## Selected next batch: validation of existing writeback fields

Work on one combined schema/validation batch before adding another translator:

1. Add fixed type/count/singleton rules for the **64 existing reverse targets**
   identified above. Reuse the existing field validators for enums, bitfields,
   rational sentinels, arrays and text; reconcile any differences explicitly.
   Check detailed tag definitions instead of generating wire rules from summary
   table types alone.
2. Recognize the standard InteropIFD field and legal reused IDs. Cover the
   already registered Interop extensions where needed to avoid false
   `WrongIfd` results, while retaining diagnostics for truly misplaced tags.
3. Verify one consistent native contract through generic authoring, exact-key
   editing, translation and serialization. Include valid and malformed inputs,
   wire hints, unknown-tag policies, duplicates, limits and rollback. Preserve
   documented sentinel values and existing valid translator output.
4. Qualify the combined capture/GPS fixture, portable output, snapshots and
   container round trips together. Run the full platform matrix once against
   the final implementation, with focused checks during development.

Keep unknown/private-tag policy explicit. This batch must not turn a detached
store into a mandatory complete-file profile or infer missing companions.
Any acceptance tightening for malformed known fields must be documented.
Synchronization of conflicting shared-object access remains host-owned.

## Remaining writeback families

Only three EXIF 3.0 ExifIFD data fields lack a dedicated reverse route:

| Field | Existing boundary | Next decision |
| --- | --- | --- |
| ColorSpace `A001` | Host target override exists | Define color/ICC/Interop authority and agreement rules. |
| RelatedSoundFile `A004` | Exact-key storage only | Define filename validation and host-owned file association. |
| MakerNote `927C` | Opaque vendor data | Keep relocation and rewrite trust in the separate vendor workstream. |

EXIF 3.1 adds seven fields. The **six development/correction fields**
`A40D`–`A412` are implemented in 0.5.11:

- DevelopmentType: one SHORT packs two defined byte values, each chosen from
  `1`, `2` or `4`. XMP represents them as a structure, not one scalar enum.
- DevelopmentTypeDescription: native UTF-8 TIFF type `129`, including when the
  characters happen to be ASCII. It needs an explicit serializer rule.
- DistortionCorrection, ChromaticAberrationCorrection and ShadingCorrection:
  SHORT values `0` or `1`.
- NoiseReduction: SHORT values `0` through `3`.

The published XMP structure uses `exifEX:DevelopmentCharacterstic` and
`exifEX:FactoryDefault`. The former spelling is intentional here: it matches
DC-010-2026 Table A.6. Use the namespace URI `http://cipa.jp/exif/1.0/` as the
identity. The host supplies EXIF version 0300 or 0310; the translator does not
change or infer ExifVersion. Compatible-file and rendered-image transfer retain
present native and XMP values; no processing history is inferred from RAW or
MakerNote data.

LearningOptOutIn `9287` remains the next EXIF 3.1 writeback decision because its
structured usage and intention data needs a separate bounded contract.

The implementation records a source inconsistency that requires an explicit
host policy:
DC-008-2026 section 4.6.6.1.1 specifies `0300` in both its English edition
(PDF page 58) and [Japanese original][exif31j] (PDF page 60), while DC-010-2026
Table 7 describes EXIF 3.1 as `0310` (PDF page 14). Do not silently repair the
published text or automatically upgrade a caller's version. Record the chosen
host-supplied version policy and its limits.

LearningOptOutIn `9287` is the seventh addition. Its structured usage/intention
data needs a separate bounded contract; the library must not invent the user's
intentions.

## Later profile work

IFD0 ImageDescription `010E`, Artist `013B` and Copyright `8298` lack dedicated
reverse routes; the descriptive reverse API currently targets IPTC only.
Broader EXIF 3 UTF-8 support and Copyright's two-part NUL-separated representation
need explicit encoding contracts. Artist/owner/photographer and Software/editor
relationships belong in the same profile review.

The other twenty unrouted Table 6 fields describe raster, color or layout
facts. Their host/encoder authority must be defined before adding copying
rules. Full profile validation also needs explicit file/container and version
context for mandatory tags, color/Interop agreement and companion requirements.
The current dimensions/CFA validation context does not supply that profile.

Downstream OIIO/iRAW application acceptance remains on hold. This audit does not
change C/Rust implementations or vendor rewrite guarantees. Fuzzy search remains
the lowest priority.

[exif30]: https://www.cipa.jp/std/documents/download_e.html?CIPA_DC-008-2024-E=
[exif31]: https://www.cipa.jp/std/documents/download_e.html?CIPA_DC-008-2026-E=
[exif31j]: https://www.cipa.jp/std/documents/download_j.html?CIPA_DC-008-2026-J=
[xmp31]: https://www.cipa.jp/std/documents/download_e.html?CIPA_DC-010-2026=
