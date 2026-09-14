# Metadata Translation

`openmeta/metadata_translation.h` provides bounded explicit projection between
metadata families. Current contracts translate edited XMP creation dates into
native EXIF/IPTC date groups, exact technical XMP/TIFF properties into native
EXIF fields, typed capture properties into native EXIF scalars, target-bound
image geometry into native TIFF/EXIF groups, and exact descriptive, flat
location, and editorial XMP properties into native IPTC-IIM datasets before writing.

The APIs are experimental and versioned by
`kMetadataDateTranslationContractVersion == 1` and
`kMetadataTechnicalTranslationContractVersion == 1` and
`kMetadataCaptureTranslationContractVersion == 1` and
`kMetadataGeometryTranslationContractVersion == 1` and
`kMetadataDescriptiveTranslationContractVersion == 1` and
`kMetadataLocationTranslationContractVersion == 1` and
`kMetadataEditorialTranslationContractVersion == 1` and
`kMetadataIptcTranslationContractVersion == 1` and
`kMetadataGpsTranslationContractVersion == 1` and
`kMetadataStructuredLocationTranslationContractVersion == 1` and
`kMetadataGpsNavigationTranslationContractVersion == 1`.

## Workflow

Translation is a separate step. Creation, editing, transfer, and writing do not
invoke it implicitly:

1. Read, create, or edit a finalized `MetaStore`.
2. Call `translate_xmp_creation_dates(...)`,
   `translate_xmp_technical_metadata(...)`,
   `translate_xmp_capture_metadata(...)`,
   `translate_xmp_image_geometry(...)`,
   `translate_xmp_descriptive_metadata(...)`,
   `translate_xmp_location_metadata(...)`,
   `translate_xmp_editorial_metadata(...)`,
   `translate_xmp_iptc_metadata(...)`, `translate_xmp_gps_metadata(...)`,
   `translate_xmp_structured_location_metadata(...)`,
   `translate_xmp_gps_navigation_metadata(...)`,
   or the required combination with
   explicit mapping and conflict options.
3. Pass the returned finalized store to transfer preparation or a writer.

This separation prevents a transfer from unexpectedly replacing native camera
dates merely because decoded XMP is present. The default source mode is
`DirtyOnly`, so only caller-modified XMP values and tombstones are eligible.

## Date Mappings

| XMP source | Native destination | Precision requirements |
| --- | --- | --- |
| `xmp:CreateDate` | EXIF `DateTimeDigitized`, `OffsetTimeDigitized`, and `SubSecTimeDigitized` | Time is required. Timezone and up to nine fractional digits are preserved in companion tags. |
| `xmp:CreateDate` | IPTC `DigitalCreationDate` and `DigitalCreationTime` | Date-only or whole seconds are accepted. Fractional seconds are rejected. |
| `photoshop:DateCreated` | IPTC `DateCreated` and `TimeCreated` | Date-only or whole seconds are accepted. Fractional seconds are rejected. |
| XMP `exif:DateTimeOriginal` | EXIF `DateTimeOriginal`, `OffsetTimeOriginal`, and `SubSecTimeOriginal` | Time is required. Timezone and up to nine fractional digits are preserved in companion tags. |

Accepted values use a full Gregorian `YYYY-MM-DD` date, optionally followed by
`T` and `hh:mm:ss`, up to nine fractional digits, and `Z` or a `+/-HH:MM`
timezone. Invalid dates, malformed values, and conversions that would discard
precision fail instead of being normalized or truncated. Lexical `-00:00` is
preserved as a negative-zero offset.

Each mapping can be disabled independently. For example, callers that need to
retain fractional `xmp:CreateDate` can disable its IPTC projection while
keeping exact EXIF projection.

## Technical EXIF Mappings

| XMP source | Native EXIF destination | Requirements |
| --- | --- | --- |
| `xmp:ModifyDate` | IFD0 `DateTime` plus ExifIFD `OffsetTime` and `SubSecTime` | Time is required. Timezone and up to nine fractional digits are preserved in companion tags. |
| `tiff:Make` | IFD0 `Make` | Non-empty 7-bit ASCII without embedded NUL bytes. |
| `tiff:Model` | IFD0 `Model` | Non-empty 7-bit ASCII without embedded NUL bytes. |
| `xmp:CreatorTool` | IFD0 `Software` | Non-empty 7-bit ASCII without embedded NUL bytes. |

Namespaces and property paths must match exactly. These mappings are intended
for edited or newly created host metadata, not for copying source-bound camera
processing data. Each singleton and the complete `ModifyDate` companion group
reconcile independently, so a conflict in `Make` does not silently change
`Model`.

## Capture EXIF Mappings

| XMP source | Native EXIF destination | Required native type |
| --- | --- | --- |
| `exif:ExposureTime` | ExifIFD `ExposureTime` | One unsigned `RATIONAL`, greater than zero |
| `exif:FNumber` | ExifIFD `FNumber` | One unsigned `RATIONAL`, greater than zero |
| `exif:ISO`, `exif:ISOSpeedRatings`, or `exif:ISOSpeedRatings[1]` | ExifIFD `ISOSpeedRatings` | One `SHORT` in `1..65535` |
| `exif:FocalLength` | ExifIFD `FocalLength` | One unsigned `RATIONAL`, greater than zero |
| `exif:ExposureCompensation` or `exif:ExposureBiasValue` | ExifIFD `ExposureBiasValue` | One signed `SRATIONAL` |

Rational sources may be typed scalar XMP values or full text integers,
decimals, scientific decimals, and `numerator/denominator` values. Focal length
also accepts the OpenMeta portable ` mm` suffix. Conversion uses integer
arithmetic and reduces the exact source value before checking the 32-bit EXIF
numerator and denominator limits. It never uses a floating-point approximation.
For example, `2.8` becomes `14/5` and `8e-3` becomes `1/125`.

This exactness is intentionally strict. A bounded repeating decimal such as
`0.333333333333333` does not fit native `SRATIONAL` exactly and returns
`ValueOutOfRange`; provide `1/3` or a typed signed rational when exact thirds
are required. ISO rejects multi-value arrays, decimals, zero, and values above
`65535` instead of selecting, truncating, or changing the native TIFF type.

Portable and standard aliases target the same native singleton. If more than
one eligible alias is present, the source is ambiguous and translation fails
rather than selecting one.

## Capture settings writeback

`translate_xmp_capture_settings_metadata(...)` and Python
`Document.translate_capture_settings_metadata(...)` add twelve independent
SHORT singleton mappings with `MetadataCaptureSettingsTranslationOptions`,
contract version 1. Exact unindexed paths use `http://ns.adobe.com/exif/1.0/`.

| XMP path | ExifIFD tag | Accepted codes | Flag |
| --- | --- | --- | --- |
| ExposureProgram | 0x8822 | 0..8 | exposure_program_to_exif |
| MeteringMode | 0x9207 | 0..6, 255 | metering_mode_to_exif |
| SensingMethod | 0xA217 | 1..5, 7, 8 | sensing_method_to_exif |
| CustomRendered | 0xA401 | 0..1 | custom_rendered_to_exif |
| ExposureMode | 0xA402 | 0..2 | exposure_mode_to_exif |
| WhiteBalance | 0xA403 | 0..1 | white_balance_to_exif |
| SceneCaptureType | 0xA406 | 0..3 | scene_capture_type_to_exif |
| GainControl | 0xA407 | 0..4 | gain_control_to_exif |
| Contrast | 0xA408 | 0..2 | contrast_to_exif |
| Saturation | 0xA409 | 0..2 | saturation_to_exif |
| Sharpness | 0xA40A | 0..2 | sharpness_to_exif |
| SubjectDistanceRange | 0xA40C | 0..3 | subject_distance_range_to_exif |

Sources accept signed or unsigned scalar integers in the listed sets, unsigned
decimal integer text, or exact existing OpenMeta enum labels. SceneCaptureType
also accepts the portable label `Night scene` for code 3. Text must use Ascii,
Utf8, or Unknown encoding. Case changes, whitespace, signs in text, decimals,
fractions, floats, arrays, and unknown codes fail. ExposureProgram code 9/Bulb
remains a read-only extension. No camera state or rendering behavior is inferred.

Defaults are DirtyOnly/FailOnConflict with every field enabled. Eligible
duplicate sources fail; clean sources are ignored in DirtyOnly, following the
existing numeric capture contract. Native values must be one SHORT and equal
to the selected code. PreserveExisting keeps existing fields, FailOnConflict
protects non-equivalent values, and ReplaceExisting repairs types/duplicates.
Dirty source tombstones remove the corresponding field under ReplaceExisting.
All selected fields share one atomic transaction, including source/output
aliasing and owned provenance. Existing numeric capture options remain unchanged.

Limits are 12 added entries, 1024 operations, 128 source text bytes per selected
active property, and 1536 total source text bytes. Limits may be lowered and
never cause truncation. Preparation may allocate. EXIF versions are retained;
the API does not create or upgrade version metadata.

Capture coverage has no fixed all-tags denominator. Camera/lens/spectral text
and direct APEX values have separate contracts below. Focal-plane/subject arrays
remain open; these twelve settings do not close arbitrary EXIF writeback.


## Exact capture rational writeback

`translate_xmp_capture_rational_metadata(...)` and Python
`Document.translate_capture_rational_metadata(...)` use
`MetadataCaptureRationalTranslationOptions`, contract version 1, for four
independent unsigned RATIONAL singletons. Exact unindexed source paths use
`http://ns.adobe.com/exif/1.0/`.

| XMP path | ExifIFD tag | Value contract | Flag |
| --- | --- | --- | --- |
| SubjectDistance | 0x9206 | Meters; zero unknown; wire numerator UINT32_MAX infinity | subject_distance_to_exif |
| DigitalZoomRatio | 0xA404 | Nonnegative ratio; zero means digital zoom unused | digital_zoom_ratio_to_exif |
| ExposureIndex | 0xA215 | Positive exposure index | exposure_index_to_exif |
| FlashEnergy | 0xA20B | Nonnegative BCPS energy; no Flash-state inference | flash_energy_to_exif |

Sources accept scalar nonnegative integers, unsigned rationals, and the existing
exact integer/decimal/scientific/fraction text grammar, including a leading plus.
Signed rational and floating-point values, arrays, whitespace, unit suffixes,
zero denominators, malformed numbers, and unsupported precision fail. Text must
use Ascii, Utf8, or Unknown encoding. Checked uint64 parsing precedes reduction;
the reduced numerator and denominator must fit uint32. No rounding occurs.

SubjectDistance adds exact `Unknown` and `Infinity` labels. Integer/fraction text
and typed integer/unsigned-rational inputs with numerator UINT32_MAX select
infinity before reduction; their denominator must be positive and fit uint32.
New unknown/infinity values use 0/1 and UINT32_MAX/1. Equivalent existing sentinel
encodings retain their denominator. Decimal/scientific text denotes a finite
number; if its reduced numerator would equal UINT32_MAX, translation fails to
avoid encoding a finite distance as infinity. Other finite inputs that only
reduce to that reserved numerator also fail. Native sentinel equivalence is
checked before ordinary rational cross-multiplication.

Defaults are DirtyOnly/FailOnConflict with all fields enabled. Missing sources
retain native fields; eligible duplicates fail. PreserveExisting retains native
values, FailOnConflict requires typed equivalence, and ReplaceExisting repairs
values/types/duplicates or removes a dirty source tombstone's native field.
All selected fields commit atomically, including source/output aliasing and
owned provenance. Limits are four additions, 1024 operations, 128 source text
bytes per property, and 512 total text bytes. Preparation may allocate.

The canonical EXIF tags 0xA215 and 0xA20B are the only ExposureIndex/FlashEnergy
targets; older TIFF/EP aliases remain untouched. No APEX conversion, unit
conversion, version creation/upgrade, or geographic/flash-state inference occurs.
Existing numeric and settings option layouts remain unchanged. Portable
native-to-XMP numeric formatting may be approximate; retain original XMP with
both `xmp_include_existing=True` and `XmpConflictPolicy.ExistingWins` when exact
source spelling is needed.

## Complete Flash writeback

`translate_xmp_flash_metadata(...)` and Python
`Document.translate_flash_metadata(...)` use `MetadataFlashTranslationOptions`,
contract version 1, to write one ExifIFD Flash (0x9209) SHORT. Select either an
exact scalar `exif:Flash` or all five children in the EXIF namespace:

- `Flash/Fired`: bit 0, Boolean.
- `Flash/Return`: bits 1..2, codes 0, 2, or 3; code 1 is reserved.
- `Flash/Mode`: bits 3..4, codes 0..3.
- `Flash/Function`: bit 5, Boolean; True means no flash function.
- `Flash/RedEyeMode`: bit 6, Boolean.

Children also accept the `Flash/exif:Name` spelling. Boolean values accept
integer 0/1 or exact text `True`, `False`, `0`, `1`. Mode and Return accept
nonnegative scalar integers or unsigned decimal integer text. Scalar Flash
accepts those integer forms or exact OpenMeta Flash labels. Bits 7 and above,
reserved Return code 1, floats, rationals, arrays, whitespace, signs in text,
case changes, and unknown labels fail. Text encodings are Ascii, Utf8, or Unknown.
All 96 combinations of defined bits are accepted; physical consistency is not
inferred from other capture fields.

Defaults are DirtyOnly/FailOnConflict. One dirty child selects the complete
structure, including its clean companions. A partial structure fails without
borrowing missing bits from native EXIF. Duplicate child aliases, scalar plus
structured sources, qualifiers, nested children, and indexed forms fail. Clean
unselected sources are retained. A dirty scalar tombstone or all five dirty
child tombstones removes native Flash under ReplaceExisting; partial child
removal fails. Missing sources retain native Flash. FlashEnergy is independent.

Native equivalence requires one SHORT with the same bits. PreserveExisting keeps
native fields, FailOnConflict protects non-equivalent values, and ReplaceExisting
repairs types/duplicates. All operations commit atomically, including aliased
source/output and owned provenance. Limits are one added entry, 1024 operations,
16 inspected Flash source properties, 128 text bytes per active property, and
640 total text bytes. Limits may be lowered; preparation may allocate. Existing
capture option layouts remain unchanged. EXIF version fields are retained.

For portable XMP readback, use `include_existing_xmp=True` and
`conflict_policy=XmpConflictPolicy.ExistingWins` to retain a complete source
structure or scalar spelling. Snapshot transfer uses `xmp_include_existing`
and `xmp_conflict_policy` for the same choices. Native-only portable output
continues to use the existing Flash projection.

## LightSource writeback

`translate_xmp_light_source_metadata(...)` and Python
`Document.translate_light_source_metadata(...)` use
`MetadataLightSourceTranslationOptions`, contract version 1, to write one
ExifIFD LightSource (0x9208) SHORT from exact `exif:LightSource`.

Accepted codes are `0..4`, `9..34`, and `255`: 32 values from the existing
OpenMeta name table. This includes unknown (0), other (255), warm white
fluorescent (16), and the newer light-source/LED codes 25..34. Undefined codes
fail. No source means no change; the API does not create a default Unknown.

Sources accept nonnegative signed or unsigned scalar integers, unsigned decimal
integer text, and exact unique OpenMeta labels. Explicit aliases `Tungsten` and
`Cloudy weather` map to 3 and 10. Numeric `1` and `25` remain distinct, while
text `Daylight` returns `AmbiguousSource` because both codes use that display
label. Native values, conflict policies, EXIF versions, white balance, Flash,
DNG calibration illuminants and color temperature never resolve this ambiguity.
Callers must supply an explicit numeric code when the source label is ambiguous.

Text must use Ascii, Utf8, or Unknown encoding. Signs in text, whitespace,
case changes, floating-point/scientific notation, fractions, arrays, unsupported
encodings and unknown labels fail. Eligible indexed, qualified or nested
LightSource paths return `UnsupportedSourceShape`. Foreign namespaces and other
property names are ignored. Clean sources are ignored in default DirtyOnly mode;
All selects active sources. Duplicate eligible sources fail.

Native equivalence requires one SHORT with the selected code. Default
FailOnConflict protects different values and wrong types. PreserveExisting
retains native data; ReplaceExisting repairs types and duplicates. A dirty
source tombstone removes native LightSource under ReplaceExisting. Tombstone
contents are not parsed, so an ambiguous old label can still be explicitly
removed. Clean tombstones are ignored. Source omission retains native metadata.

Limits are one addition, 1024 operations, 128 text bytes per active property,
and 128 total text bytes. Limits may be lowered; invalid settings fail. All
changes share the existing atomic capture transaction, including aliased
source/output and owned provenance. Preparation may allocate. Existing capture
option layouts and EXIF version metadata are retained. This field operation
does not claim full EXIF-version conformance or infer other capture metadata.

Portable native-to-XMP output now emits numeric `1` and `25` for these two
LightSource values, preserving their distinction. Other defined values retain
the existing unique labels. Every defined native code survives portable dump,
XMP decode and this reverse API. Existing XMP retained with ExistingWins keeps
its original spelling, including ambiguous `Daylight`; previously lost numeric
identity cannot be recovered from that text alone. Human-readable native names
and DNG calibration-illuminant display names remain unchanged.

## Target-Bound Image Geometry

`translate_xmp_image_geometry(...)` projects edited XMP image geometry only
when it agrees with a host-supplied `TransferTargetImageSpec`:

| XMP source | Native EXIF destination | Required target fact |
| --- | --- | --- |
| `tiff:Orientation` | IFD0 `Orientation` as one `SHORT` | `has_orientation` and the same EXIF index in `1..8` |
| `tiff:ImageWidth`, `exif:ExifImageWidth`, or `exif:PixelXDimension` plus a matching height alias | IFD0 `ImageWidth`/`ImageLength` and ExifIFD `PixelXDimension`/`PixelYDimension`, each as one `LONG` | `has_dimensions` and the same nonzero stored width/height |

Height aliases are `tiff:ImageLength`, portable `tiff:ImageHeight`,
`exif:ExifImageHeight`, and `exif:PixelYDimension`. Standard and portable
aliases may coexist when all values agree. Duplicate exact properties or
contradictory aliases fail instead of selecting one.

Width and height always describe the stored target raster. They are not
post-orientation display dimensions and are never swapped for orientation
indices `5..8`. For example, a stored `640x480` raster with orientation `6`
must use target width `640`, target height `480`, and orientation `6`; a host
that physically rotates the pixels must instead provide the new stored
dimensions and the orientation that applies to those output pixels.

An active source without the corresponding target fact returns
`TargetImageSpecRequired`. A source value that disagrees with the target, or a
dirty deletion while the target still declares that fact, returns
`TargetImageSpecMismatch`. Width and height are one complete group: mixed
active/deleted state or only one axis returns `IncompleteSourceGroup`.

## Descriptive Mappings

| XMP source | Native IPTC-IIM destination | Maximum encoded bytes |
| --- | --- | --- |
| `dc:title[@xml:lang=x-default]` | `ObjectName` (2:5) | 64 |
| `dc:description[@xml:lang=x-default]` | `Caption-Abstract` (2:120) | 2000 |
| `dc:creator[n]` | repeated `By-line` (2:80) | 32 per value |
| `dc:subject[n]` | repeated `Keywords` (2:25) | 64 per value |
| `dc:rights[@xml:lang=x-default]` | `CopyrightNotice` (2:116) | 128 |
| `photoshop:Credit` | `Credit` (2:110) | 32 |
| `photoshop:Source` | `Source` (2:115) | 32 |

The default-language and indexed paths must match exactly. Creator and keyword
items retain numeric XMP index order. Duplicate singleton properties or
duplicate active indexes are ambiguous and fail rather than selecting a value.
IPTC-IIM limits are byte limits; text is never truncated.

Non-ASCII values remain UTF-8. Translation emits IPTC `CodedCharacterSet`
(1:90) with `ESC % G` when the marker is absent and every existing active IPTC
value is ASCII or will be replaced by the same transaction. An incompatible
charset marker or unrelated legacy high-bit data returns
`NativeEncodingConflict` without modifying the output.

## Location Mappings

`translate_xmp_location_metadata(...)` accepts
`MetadataLocationTranslationOptions` and returns
`MetadataDescriptiveTranslationResult`. It uses the descriptive source modes,
conflict policies, statuses, and mapping diagnostics. The existing descriptive
API still selects only its original seven mappings.

| Exact XMP source | Native IPTC-IIM destination | Maximum encoded bytes |
| --- | --- | --- |
| `photoshop:City` | `City` (2:90) | 32 |
| `Iptc4xmpCore:Location` | `Sub-location` (2:92) | 32 |
| `photoshop:State` | `Province-State` (2:95) | 32 |
| `photoshop:Country` | `Country-PrimaryLocationName` (2:101) | 64 |
| `Iptc4xmpCore:CountryCode` | `Country-PrimaryLocationCode` (2:100) | 3 |

These are the flat legacy location mappings in the
[IPTC Photo Metadata Standard](https://www.iptc.org/std/photometadata/specification/IPTC-PhotoMetadata-2025.1.html).
Each mapping is an independent singleton. Namespace URIs and property paths
must match exactly; duplicate active sources are rejected even when equal.
Empty values, invalid UTF-8/XML text, embedded NULs, and excess bytes fail
without changing the output. Removal uses a dirty tombstone.

Country codes require exactly two or three uppercase ASCII letters and are
preserved byte for byte. Code membership, conversion between two and three
letters, and agreement with the country name belong to the caller. Structured
`LocationCreated`/`LocationShown` properties, indexed or qualified forms, and
GPS are not aliases and are not selected by this API. The operation does not
infer whether a flat location describes the camera or the depicted subject.

UTF-8 charset promotion follows the descriptive policy above, including
rejection of incompatible markers and unrelated legacy high-bit IPTC values.
Limits are 1024 matched source properties, 4096 operations, 8 MiB of inspected
text/charset-safety bytes, and six added entries (five datasets plus one charset
marker). Options can lower these limits. New entries copy source provenance;
updates preserve the existing native entry's provenance. Translation is a
preparation operation that may allocate; it is not an allocation-free replay API.

## Structured Location Reconciliation

`translate_xmp_structured_location_metadata(...)` accepts
`MetadataStructuredLocationTranslationOptions` and returns the shared
`MetadataDescriptiveTranslationResult`. Contract version 1 selects one
structured location and reconciles its five supported text fields into **both
flat legacy XMP and native IPTC-IIM**. Existing flat-location and combined IPTC
APIs keep their original mapping sets.

The root namespace must be `http://iptc.org/std/Iptc4xmpExt/2008-02-29/`.
`location_kind` defaults to `MetadataStructuredLocationKind::Shown`;
`Created` requires an explicit choice. Shown and Created never fall back to
each other. These properties distinguish the depicted place from the place
where the camera was. Choosing Created authorizes copying that camera-location
description into the legacy fields whose historical semantics are ambiguous.
See the [IPTC Photo Metadata specification](https://www.iptc.org/std/photometadata/specification/IPTC-PhotoMetadata-2025.1.html).

| Structured child | Flat XMP destination | Native IPTC | Maximum UTF-8 bytes |
| --- | --- | --- | --- |
| `City` | `photoshop:City` | 2:90 | 32 |
| `Sublocation` | `Iptc4xmpCore:Location` | 2:92 | 32 |
| `ProvinceState` | `photoshop:State` | 2:95 | 32 |
| `CountryName` | `photoshop:Country` | 2:101 | 64 |
| `CountryCode` | `Iptc4xmpCore:CountryCode` | 2:100 | 3 |

Paths are `LocationShown[n]/City`, `LocationCreated[n]/City`, and the equivalent
paths for the other four children. The existing scalar `LocationCreated/City`
resource form is accepted as record index 1. Mixed scalar and indexed Created
forms fail. Indexes must be positive decimal uint32 values without leading
zeros. `location_index == 0` selects the sole represented record when present;
an absent root kind is a no-op, and multiple records fail with AmbiguousLocation.
A positive option selects that exact index
and fails with LocationNotFound if absent. It never selects the first record
implicitly or merges records. Any represented child, including an unsupported
child, counts for record selection. Selection precedes DirtyOnly filtering.

XML decoding uses one-based RDF order; authored stores can have sparse indexes.
Canonical unqualified child names and `Iptc4xmpExt:`-qualified equivalents are
accepted. Duplicate field sources are ambiguous, including equal values,
qualified/unqualified aliases, or mixed active and dirty-deleted entries.
Other namespaces, nested Address children, language/index qualifiers, location
names and IDs, WorldRegion, and structured GPS fields are retained without
projection. Nonstandard scalar LocationShown and malformed indexed paths fail
with UnsupportedSourceShape. The API operates on decoded paths and does not
infer a missing RDF container type.

Defaults are DirtyOnly and FailOnConflict. Only the selected structured leaf
flags trigger a field in DirtyOnly mode; dirty flat/native destinations do not.
All selects clean active leaves too. Each flat XMP/native IPTC pair is one
conflict group:

- PreserveExisting retains both destinations if either already exists.
- FailOnConflict requires each existing destination to match and fills a
  missing counterpart. Mismatches and duplicate destinations fail atomically.
- ReplaceExisting updates both destinations and removes duplicate entries.
  A dirty structured leaf tombstone removes both corresponding destinations.

Missing structured fields preserve existing destination fields. Clean
tombstones and whole-structure/array tombstones do not request field deletion.
This is a field patch operation; it does not establish geographic consistency
between supplied fields and retained fields. Text must be nonempty valid
UTF-8/XML text. Byte limits never truncate. Country codes require two or three
uppercase ASCII letters; registry membership and name/code agreement remain
caller responsibilities.

Limits are 1024 inspected structured properties across the chosen root kind,
4096 destination operations, 8 MiB of source text and charset-safety inspection,
and 11 added entries: five flat XMP properties, five IPTC datasets, and a possible
UTF-8 marker. Existing destination entries also count against the operation
inspection limit. Each limit can be lowered. The shared IPTC charset policy
rejects unsafe promotion of retained legacy high-bit values. Result entry
counters include both destination families; group counters count five fields.
`NativeConflict` covers either destination family.

The source stays immutable. Failure leaves the output unchanged; output/source
aliasing is supported. New entries own copied source provenance, while updates
retain existing destination provenance. Preparation may allocate.

```python
translated = document.translate_structured_location_metadata(
    location_kind=openmeta.MetadataStructuredLocationKind.Shown,
    location_index=2,
    source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
    conflict_policy=openmeta.MetadataDescriptiveTranslationConflictPolicy.ReplaceExisting,
)
```

The returned document is detached. Persist both XMP and IPTC from its snapshot
to keep both destinations synchronized. For Python `transfer_snapshot_file`,
set `xmp_include_existing=True`: that transfer API defaults to projecting native
metadata only. This option retains the reconciled flat XMP and structured
records. GPS translation is a separate explicit contract; structured location
coordinates are not aliases for primary EXIF GPS.

## Structured location construction

`translate_xmp_location_to_structured_metadata(...)` and Python
`Document.translate_location_to_structured_metadata(...)` copy the five flat
legacy XMP location fields into one selected IPTC Extension record.
`MetadataLocationCreationTranslationOptions` has contract version 1.

| Exact flat source | Selected structured child |
| --- | --- |
| photoshop:City | City |
| Iptc4xmpCore:Location | Sublocation |
| photoshop:State | ProvinceState |
| photoshop:Country | CountryName |
| Iptc4xmpCore:CountryCode | CountryCode |

The caller must choose both `location_kind` (Shown or Created) and a positive
`location_index`. C++ defaults to Unspecified/zero and rejects those values;
Python requires both arguments. Created permits index one only. Shown permits
an existing record or append at the next index. Existing indexes must be dense
from one, with a maximum of 1024. Sparse indexes, mixed scalar/indexed Created
records, opaque root/record placeholders, and competing nested or language-qualified shapes
for a selected field fail. Other namespaces and nested Address fields are not
aliases. No Created/Shown selection or geographic information is inferred.

New records use indexed `LocationShown[n]` or `LocationCreated[1]` paths with
IPTC Extension children. Existing scalar Created records retain their resource
shape. Portable XMP emits indexed locations as RDF Bags. The decoder omits
redundant child prefixes within the same namespace; record order and field
values are preserved. RDF indexes identify current order, not stable record IDs.

City, sublocation, and state use the existing 32-byte flat-location limit;
country uses 64 bytes. CountryCode requires two or three uppercase ASCII
letters, without checking country membership. Other fields require nonempty
valid UTF-8 text, with Ascii/Utf8/Unknown source encoding. Existing flat location
text validation applies. There is no normalization or truncation.

Defaults are DirtyOnly/FailOnConflict with five enabled flags. Missing source
fields retain destinations. Duplicate selected sources fail. Each destination
leaf is a conflict group; PreserveExisting keeps it, FailOnConflict requires
equivalence, and ReplaceExisting replaces it and removes duplicate aliases.
A dirty flat-field tombstone removes that selected structured leaf. Unrelated
fields and records remain untouched. Removal of an entire non-last record fails
to prevent renumbering another record during serialization. Removing the last
record is allowed. All selected changes commit atomically, including aliased
source/output calls. Preparation may allocate.

Limits are five additions, 1024 operations, 1024 total inspected destination
properties plus selected source candidates, and the existing descriptive total
text budget. Native IPTC fields and charset markers are not modified or used
as implicit sources. Native-only callers must explicitly prepare flat XMP first.
To persist the constructed XMP, transfer requires `xmp_include_existing=True`
and `xmp_conflict_policy=openmeta.XmpConflictPolicy.ExistingWins`.

Python example:

```python
created = document.translate_location_to_structured_metadata(
    openmeta.MetadataStructuredLocationKind.Shown, 1,
    source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
)
```


## Editorial Mappings

`translate_xmp_editorial_metadata(...)` accepts
`MetadataEditorialTranslationOptions` and returns
`MetadataDescriptiveTranslationResult`. Each mapping has an independent flag;
`DirtyOnly` and `FailOnConflict` are the defaults. Descriptive and location
calls retain their existing mapping sets.

| Exact XMP source | Native IPTC-IIM destination | Maximum encoded bytes |
| --- | --- | --- |
| `photoshop:Headline` | `Headline` (2:105) | 256 |
| `photoshop:Instructions` | `SpecialInstructions` (2:40) | 256 |
| `photoshop:TransmissionReference` | `OriginalTransmissionReference` (2:103) | 32 |

The mappings and limits follow the
[IPTC Photo Metadata Standard 2025.1](https://www.iptc.org/std/photometadata/specification/IPTC-PhotoMetadata-2025.1.html).
Sources must be exact singleton properties in the Photoshop namespace
`http://ns.adobe.com/photoshop/1.0/`. Title, description, rights usage terms,
and other job identifiers are not aliases. Empty text, invalid UTF-8/XML,
embedded NULs, duplicate active singletons, and excess encoded bytes fail
transactionally. Values are never truncated. Dirty tombstones request removal
under `ReplaceExisting`.

The shared IPTC transaction enforces charset safety, preserves unrelated
native data, copies source provenance for new entries, and retains native
provenance for updates. Limits are 1024 matched source properties, four added
entries (three datasets plus one UTF-8 charset marker), 4096 operations, and
8 MiB of inspected text/charset-safety bytes. Options may lower these limits;
disabling every mapping is invalid. This preparation operation may allocate.

Python exposes the same operation as `Document.translate_editorial_metadata`,
with `headline_to_iptc`, `instructions_to_iptc`, and
`transmission_reference_to_iptc` flags. It returns a detached document and raises
`ValueError` with mapping diagnostics on failure. For clean metadata read from
a file, explicitly select `MetadataDescriptiveTranslationSourceMode.All`.

## Combined IPTC Writeback

`translate_xmp_iptc_metadata(...)` accepts `MetadataIptcTranslationOptions`
and returns `MetadataDescriptiveTranslationResult`. It selects the seven
descriptive, five location, and three editorial groups above, plus the five
workflow mappings below. All 20 text/priority groups share one transaction,
including charset promotion and resource accounting. The existing subgroup
APIs retain their original options and mappings. Date/time fields remain in
`translate_xmp_creation_dates(...)`; composing the two calls does not make
them one transaction.

| Exact Photoshop XMP source | Native IPTC-IIM destination | Contract |
| --- | --- | --- |
| `AuthorsPosition` | `By-lineTitle` (2:85) | Singleton text, at most 32 UTF-8 bytes |
| `CaptionWriter` | `Writer-Editor` (2:122) | Singleton text, at most 32 UTF-8 bytes |
| `Category` | `Category` (2:15) | One to three ASCII letters, case preserved |
| `SupplementalCategories[n]` | Repeated `SupplementalCategories` (2:20) | Text, at most 32 UTF-8 bytes per value |
| `Urgency` | `Urgency` (2:10) | One text digit from `1` to `8`, or a signed/unsigned integer scalar in that range |

These contracts use the
[Adobe Photoshop XMP namespace](https://developer.adobe.com/xmp/docs/xmp-namespaces/photoshop/),
[IPTC IIM 4.2](https://www.iptc.org/std/IIM/4.2/specification/IIMV4.2.pdf), and
[IPTC Photo Metadata 2025.1](https://www.iptc.org/std/photometadata/specification/IPTC-PhotoMetadata-2025.1.html).
Category, Supplemental Categories, and Urgency are legacy IIM fields. This API
supports their explicit writeback; it does not convert them to modern taxonomy.
Category does not consult a provider registry. The caller must supply a creator
and associate AuthorsPosition with the first creator. Translation neither
infers that association nor creates a missing creator.

Supplemental Categories use exact indexed paths with positive numeric indexes.
Gaps are accepted. Values are written in index order, and equal values at
different indexes remain separate datasets. Duplicate active indexes fail.
New repeated native entries receive placement ranks that preserve that order;
source block and wire provenance remain owned by the output. Updates retain
the existing native ranks.
An unindexed scalar or empty Bag is not a removal request; remove the indexed
entries with dirty tombstones. Urgency emits an ASCII digit, never a binary
integer. Values `0` and `9`, floats, rationals, arrays, and alternative textual
spellings such as `05`, `+5`, or `5.0` are rejected. IPTC-to-portable-XMP
projection now also carries native Urgency.

All mappings default to enabled, with DirtyOnly/FailOnConflict policies.
Each has an independent boolean flag. Disabling every mapping is invalid.
Limits across the whole call are 1024 matched source properties, 1025 added
entries (up to 1024 values plus one charset marker), 4096 operations, and
8 MiB of inspected text/charset-safety bytes. Callers may lower these limits.

A failure in any selected group leaves the source and output unchanged. This
also permits a non-ASCII replacement when another selected group removes the
legacy IPTC bytes that would otherwise block UTF-8 promotion. Preparation may
allocate; it is not an allocation-free replay operation.

Python exposes `Document.translate_iptc_metadata(...)` with the same flags,
policies, limits, detached output, and ValueError diagnostics. For example:

```python
translated = document.translate_iptc_metadata(
    source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
    conflict_policy=openmeta.MetadataDescriptiveTranslationConflictPolicy.ReplaceExisting,
    category_to_iptc=False,
    urgency_to_iptc=False,
)
```

The combined API and existing date API cover the 24 active IPTC destination
assignments in ExifTool's
[`xmp2iptc.args` inventory](https://raw.githubusercontent.com/exiftool/exiftool/master/arg_files/xmp2iptc.args)
checked on 2026-09-09. This is a field inventory, not semantic or behavioral
parity: the two commented taxonomy mappings, Photoshop IPTCDigest, arbitrary
IPTC datasets, and ExifTool's conversion/overwrite conventions are excluded.

## Primary GPS Writeback

`translate_xmp_gps_metadata(...)` accepts `MetadataGpsTranslationOptions`
and returns `MetadataGpsTranslationResult`. Contract version 1 covers three
primary EXIF GPS groups, each with an independent flag. It does not select
structured IPTC locations, destination coordinates, GPS time, navigation data,
or nonstandard standalone XMP latitude/longitude reference properties.

| Exact `http://ns.adobe.com/exif/1.0/` XMP source | Native `gpsifd` fields | Flag |
| --- | --- | --- |
| `GPSLatitude` | LatitudeRef (1), Latitude (2) | `latitude_to_exif` |
| `GPSLongitude` | LongitudeRef (3), Longitude (4) | `longitude_to_exif` |
| `GPSAltitude` and `GPSAltitudeRef` | AltitudeRef (5), Altitude (6) | `altitude_to_exif` |

Coordinates accept unsigned integer degrees followed by decimal minutes, or
integer minutes and decimal seconds: `35,48.125N` or `139,34,55.25W`.
Separators are commas; the final character must be uppercase N/S for latitude
or E/W for longitude. Signs, whitespace, decimal degrees, exponent notation,
and rational components are rejected. Latitude is bounded by 90 degrees and
longitude by 180; minutes and seconds must be less than 60. At the maximum
degree, all remaining components must be zero. Hemisphere is preserved at zero.

The parser uses integer arithmetic and emits normalized integer degrees,
integer minutes, and reduced rational seconds as RATIONAL[3], with an ASCII
reference. Fractions must fit exact unsigned 32-bit numerator/denominator
components; unsupported precision fails without rounding. Trailing decimal
zeros do not consume precision. Parsing uses checked 64-bit intermediates;
overflow also returns UnsupportedPrecision, even if further algebraic reduction
could fit the native fields. Native equivalence compares typed components
as rational numbers; it does not rearrange a different native DMS tuple into
an equivalent whole angle. Use ReplaceExisting to canonicalize such tuples.

Altitude uses a nonnegative unsigned rational, nonnegative integer scalar, or
exact unsigned text (`123.45`, `2469/20`, or `0`). Floats, signed text, exponent
notation, units, arrays, and zero denominators are rejected. A separate
`GPSAltitudeRef` is mandatory. This contract uses the legacy Adobe XMP sea-level
convention: text `0`/`1`, or an integer scalar 0/1. It does not infer a missing
reference from the sign or from existing native metadata.

GPSVersionID is structural companion metadata. A missing version becomes BYTE[4]
`2.3.0.0` when a selected active result needs it. An existing version must be one
well-formed BYTE[4] entry; malformed or duplicate active versions fail. Existing
version bytes are retained. Altitude supports `2.0.0.0` through `2.4.0.0`:
legacy versions use native sea-level references 0/1; version 2.4 uses 2/3 for the
same meaning. Unknown versions fail altitude translation. No geoid, ellipsoid,
horizontal datum, or unit conversion is performed, and native GPSVersionID is
not upgraded. The XMP GPSVersionID property is not copied or used to infer an
altitude convention. Callers with modern ellipsoidal-height XMP must not pass
it under this legacy sea-level contract.

Coordinate syntax and legacy XMP altitude semantics follow the
[Adobe EXIF namespace](https://developer.adobe.com/xmp/docs/xmp-namespaces/exif/)
and [CIPA's EXIF/XMP mapping](https://cipa.jp/std/documents/e/DC-X010-2017.pdf).
The version-2.4 native reference distinction follows EXIF 3.0 section 4.6.7.1.6.

Defaults are DirtyOnly and FailOnConflict. One dirty altitude member selects
both active XMP members, including a clean companion. Missing, duplicate, or
mixed active/deleted altitude members fail. A complete dirty altitude tombstone
pair removes both native fields under ReplaceExisting; one coordinate tombstone
removes its native value/reference pair. PreserveExisting preserves the existing
native group as a unit when either field exists, even when the group is partial.
FailOnConflict rejects mismatched or partial existing groups. ReplaceExisting
updates fields and removes duplicates. If deletion removes the last GPS value,
its version tag is also removed; unrelated GPS fields keep the version.

The whole call is atomic. It leaves source and output unchanged on failure and
copies provenance into output-owned storage. Calls may allocate during
preparation. Limits are seven added entries including GPSVersionID, 1024 native
operations, 128 text bytes per selected active source, and 512 total text bytes.
Coordinates and altitude can be disabled independently; disabling every group
is invalid. Existing translation APIs and their default mapping sets are unchanged.

Python exposes the same C++ policy and diagnostics:

```python
translated = document.translate_gps_metadata(
    source_mode=openmeta.MetadataGpsTranslationSourceMode.All,
    conflict_policy=openmeta.MetadataGpsTranslationConflictPolicy.ReplaceExisting,
)
```

Pass the detached result to transfer preparation to persist it. Portable XMP
now writes all four GPSVersionID components. Its existing coordinate/altitude
formatters can round native rational values; this reverse API cannot recover
precision already lost before the XMP source was created. This contract covers
primary position writeback, not all GPS metadata or complete ExifTool parity.

## GPS Time And Navigation Writeback

`translate_xmp_gps_navigation_metadata(...)` uses independent
`MetadataGpsNavigationTranslationOptions` and the shared GPS result, source
mode, conflict policy, and diagnostic enums. Contract version 1 adds four
atomic groups. The existing primary GPS API retains its options and mapping set.

| Exact EXIF XMP source | Native `gpsifd` fields | Flag |
| --- | --- | --- |
| `GPSTimeStamp` | TimeStamp (7) and DateStamp (29) | `timestamp_to_exif` |
| `GPSSpeedRef` and `GPSSpeed` | SpeedRef (12) and Speed (13) | `speed_to_exif` |
| `GPSTrackRef` and `GPSTrack` | TrackRef (14) and Track (15) | `track_to_exif` |
| `GPSImgDirectionRef` and `GPSImgDirection` | ImgDirectionRef (16) and ImgDirection (17) | `image_direction_to_exif` |

Sources must use the exact `http://ns.adobe.com/exif/1.0/` namespace and
unindexed, unqualified paths. XMP combines the native GPS date and time into
one GPSTimeStamp property. Split GPSDateStamp/time, GPSDateTime aliases,
capture-date fallback, receiver status/quality, destination GPS, and structured
IPTC locations are outside this contract. Field identities and unit/reference
codes follow the [Adobe EXIF namespace](https://developer.adobe.com/xmp/docs/xmp-namespaces/exif/)
and [CIPA EXIF/XMP mapping](https://cipa.jp/std/documents/e/DC-X010-2017.pdf).

GPSTimeStamp requires text `YYYY-MM-DDTHH:MM:SS[.fraction]Z` or the same complete
date/time followed by `+HH:MM` or `-HH:MM`. The timezone is mandatory. Numeric
offsets through 23:59 normalize to UTC, including day/month/year rollover;
both zero-offset signs mean UTC. Years must remain 0001 through 9999 after
normalization. Gregorian calendar rules apply. Hours are 00-23, minutes 00-59,
and seconds are less than 60. Leap seconds, partial dates/times, lowercase
separators, whitespace, and inferred timezones are rejected.

Output contains an ASCII `YYYY:MM:DD` date and an unsigned RATIONAL[3] time:
integer UTC hours, integer UTC minutes, and reduced exact rational seconds.
Fractions never round. Reduced numerator and denominator must each fit uint32;
checked uint64 parser intermediates can also reject unsupported precision.
Trailing decimal zeros do not consume precision. A supported fractional input
can still fail when its whole-second numerator cannot fit uint32 after reduction.

Speed and angles accept nonnegative integer scalars, unsigned rational scalars,
or exact unsigned integer/decimal/fraction text, matching primary GPS altitude
syntax. Floats, signed text, exponents, units inside numeric values, arrays,
and zero denominators are rejected. Speed has no additional bound beyond the
native rational capacity. Track and image direction are bounded by 359.99
inclusive, without rounding or modulo reduction.

Reference properties are text. Speed accepts `K`, `M`, or `N`, plus the exact
existing OpenMeta portable aliases `km/h`, `mph`, and `knots`. Track and image
direction accept `T` or `M`, plus `True North` and `Magnetic North`. Output uses
the compact ASCII letter code. Case, spelling, and whitespace are exact; no
localized names or other synonyms are accepted. Units and north references
are preserved without conversion or inference from native metadata.

All flags default to enabled, with DirtyOnly/FailOnConflict. A dirty reference
or numeric member selects its complete active XMP pair, including a clean
companion. Missing, duplicate, or mixed active/deleted members fail. A dirty
timestamp tombstone removes both date and time; removing a navigation pair
requires both members to be dirty tombstones. Clean tombstones are ignored.
Missing groups preserve native fields.

Each native pair uses the primary GPS conflict rules: PreserveExisting keeps
the whole group if either member exists; FailOnConflict requires a complete
matching pair; ReplaceExisting repairs partial pairs and removes duplicates.
Native rational equivalence is exact by component. Native text may include
its one trailing wire NUL. Existing equivalent values retain their encoding
and provenance. All selected groups share one atomic transaction.

An active timestamp requires native GPSVersionID 2.2.0.0 through 2.4.0.0 under
this versioned contract. Older and unknown versions fail without an upgrade.
Speed/track/direction retain any well-formed BYTE[4] version. Missing versions
become 2.3.0.0 when selected active output needs one. Malformed or duplicate
active versions fail. Removing the last GPS value also removes its version;
unrelated GPS fields retain it. Source XMP GPSVersionID is not copied.

Limits are nine added entries including GPSVersionID, 1024 native operations,
128 text bytes per selected active property, and 896 total text bytes. Limits
can be lowered. Failure leaves source and output unchanged, including when they
alias. New entries own copied source provenance; preparation may allocate.

```python
translated = document.translate_gps_navigation_metadata(
    source_mode=openmeta.MetadataGpsTranslationSourceMode.All,
    conflict_policy=openmeta.MetadataGpsTranslationConflictPolicy.ReplaceExisting,
)
```

Persist the detached result through a transfer snapshot with EXIF enabled.
When retaining source XMP too, set `xmp_include_existing=True` and
`xmp_conflict_policy=openmeta.XmpConflictPolicy.ExistingWins` in Python transfer
helpers. Inclusion alone keeps the historical precedence of generated EXIF
values, which can change reference spelling or timestamp formatting. ExistingWins
preserves supplied XMP values over generated counterparts. The portable writer's
native-only decimal formatting is not an exact arbitrary-rational round trip.

## Destination GPS Writeback

`translate_xmp_gps_destination_metadata(...)` uses independent
`MetadataGpsDestinationTranslationOptions` and the shared GPS result, source
mode, conflict policy, and diagnostic enums. Contract version 1 adds four
atomic groups; the primary GPS and navigation APIs keep their existing scopes.

| Exact EXIF XMP source | Native `gpsifd` fields | Flag |
| --- | --- | --- |
| `GPSDestLatitude` | DestLatitudeRef (19) and DestLatitude (20) | `latitude_to_exif` |
| `GPSDestLongitude` | DestLongitudeRef (21) and DestLongitude (22) | `longitude_to_exif` |
| `GPSDestBearingRef` and `GPSDestBearing` | DestBearingRef (23) and DestBearing (24) | `bearing_to_exif` |
| `GPSDestDistanceRef` and `GPSDestDistance` | DestDistanceRef (25) and DestDistance (26) | `distance_to_exif` |

Sources use exact unindexed, unqualified paths in
`http://ns.adobe.com/exif/1.0/`. Coordinates use the primary GPS text syntax:
`degrees,decimalMinutesN` or `degrees,minutes,decimalSecondsN`, with the
appropriate N/S or E/W hemisphere. Bounds are 90/180 degrees and minutes or
seconds below 60; components after a maximum degree must be zero. Output is
ASCII hemisphere plus exact reduced unsigned RATIONAL[3], including the
hemisphere of zero. Unsupported precision fails without rounding.

Bearing and distance use the navigation exact nonnegative integer/rational
scalar or unsigned integer/decimal/fraction text syntax. Bearing is in
[0, 359.99], with `T`/`M` or exact `True North`/`Magnetic North` aliases.
Distance accepts `K`/`M`/`N` for kilometers, miles, and nautical miles, plus
exact `Kilometers`, `Miles`, and `Nautical miles` aliases. These distance
units follow [CIPA DC-X010-2017, section 8.3](https://cipa.jp/std/documents/e/DC-X010-2017.pdf).
Portable XMP now emits `Nautical miles` for destination unit N; it previously
emitted the incorrect label `Knots`. The reverse API accepts that historical
capitalized spelling as N distance without converting the number. Lowercase
`knots`, `km/h`, and `mph` are speed references and are rejected for distance.
No bearing, distance, primary position, datum, north reference, or unit is
inferred or converted. Numeric precision follows the primary GPS parser limits.

Defaults are DirtyOnly/FailOnConflict with all four groups enabled. One dirty
bearing/distance member selects the complete active pair, including its clean
companion. Missing, duplicate, and mixed deleted/active members fail. A dirty
coordinate tombstone removes both native coordinate fields; bearing/distance
removal requires both XMP members to be dirty tombstones. Clean tombstones
are ignored. Missing groups preserve their native values.

Native pairs follow the primary GPS conflict policy. PreserveExisting retains
a whole group when either native member exists. FailOnConflict requires a
complete equivalent pair. ReplaceExisting repairs partial or malformed groups
and removes duplicates. Equivalence compares typed rational components exactly;
text may include its one wire NUL. A well-formed native BYTE[4] GPSVersionID is
retained without interpreting or upgrading it. Missing versions default to
2.3.0.0 when active output needs one. Malformed or duplicate active versions
fail. Removing the final GPS value removes its version; unrelated GPS retains it.

Limits are nine added entries including the version, 1024 operations, 128 text
bytes per selected active source, and 768 total text bytes. Limits may be
lowered. All selected groups share one transaction; failures leave source and
output unchanged, including aliasing. New entries own their provenance.
Preparation may allocate. Python returns a detached document:

```python
destination = document.translate_gps_destination_metadata(
    source_mode=openmeta.MetadataGpsTranslationSourceMode.All,
    conflict_policy=openmeta.MetadataGpsTranslationConflictPolicy.ReplaceExisting,
)
```

Persist the result through transfer preparation. To retain original XMP values
and spelling, Python transfer requires both `xmp_include_existing=True` and
`xmp_conflict_policy=openmeta.XmpConflictPolicy.ExistingWins`. Native-only
portable coordinate/rational formatting can be approximate; reverse translation
cannot recover precision already lost from its input. Receiver status/quality,
DOP, and geographic computation remain outside this contract.

## GPS receiver quality writeback

`translate_xmp_gps_quality_metadata(...)` and Python
`Document.translate_gps_quality_metadata(...)` add five independent singleton
mappings with `MetadataGpsQualityTranslationOptions`, contract version 1.
Exact unindexed paths use `http://ns.adobe.com/exif/1.0/`.

| XMP source | Native tag/type | Flag |
| --- | --- | --- |
| `GPSStatus` | 9 / ASCII | `status_to_exif` |
| `GPSMeasureMode` | 10 / ASCII | `measure_mode_to_exif` |
| `GPSDOP` | 11 / RATIONAL | `dop_to_exif` |
| `GPSDifferential` | 30 / SHORT | `differential_to_exif` |
| `GPSHPositioningError` | 31 / RATIONAL | `horizontal_error_to_exif` |

Status accepts exact A/V or Measurement Active/Measurement Void text. Measure
mode accepts nonnegative integer 2/3 or exact text `2`/`3`. Differential accepts
integer 0/1, exact text `0`/`1`, or No Correction/Differential Corrected. No other
enum spellings, decimal/fraction coercion, or unknown codes are accepted.
DOP and horizontal error use the existing exact nonnegative rational parser;
floats, signed text, exponents, zero denominators, and unsupported precision fail.
DOP is retained without calculating an accuracy estimate. Horizontal error is
in meters; no unit conversion or receiver-state inference occurs. The field
identities follow [CIPA EXIF/XMP mapping](https://cipa.jp/std/documents/e/DC-X010-2017.pdf).

The v1 version contract accepts GPS 2.2 through 2.4 for differential correction
and GPS 2.3 through 2.4 for horizontal error. Earlier or unknown versions fail
when the corresponding active field is selected. Other quality fields retain
any well-formed native BYTE[4] version. Versions are never upgraded. A missing
native version defaults to 2.3.0.0; source XMP GPSVersionID is not copied.

Defaults are DirtyOnly/FailOnConflict and five enabled flags. One dirty source
tombstone removes its native singleton. Missing fields and clean tombstones
retain native data. Duplicate sources fail. PreserveExisting retains existing
native values; FailOnConflict requires typed equivalence; ReplaceExisting
repairs malformed values and duplicates. Native ASCII permits one trailing
wire NUL; rational equivalence is exact. All five selected fields share one
atomic transaction. Removing the last GPS field also removes its version,
while unrelated GPS retains it. New entries own their provenance, and failures
preserve source/output even when they alias.

Limits are six added entries including version, 1024 operations, 128 text bytes
per selected active source, and 640 total text bytes. Limits can be lowered.
Preparation may allocate. Existing primary, navigation, and destination APIs
keep their scopes. Python returns a detached document for transfer preparation.
Source XMP preservation still requires both `xmp_include_existing=True` and
`xmp_conflict_policy=openmeta.XmpConflictPolicy.ExistingWins` during transfer.

## GPS text writeback

`translate_xmp_gps_text_metadata(...)` and Python
`Document.translate_gps_text_metadata(...)` add four independent singleton
mappings with `MetadataGpsTextTranslationOptions`, contract version 1.
Exact unindexed paths use `http://ns.adobe.com/exif/1.0/`.

| XMP source | Native tag/type | Flag |
| --- | --- | --- |
| `GPSSatellites` | 8 / ASCII | `satellites_to_exif` |
| `GPSMapDatum` | 18 / ASCII | `map_datum_to_exif` |
| `GPSProcessingMethod` | 27 / UNDEFINED encoded text | `processing_method_to_exif` |
| `GPSAreaInformation` | 28 / UNDEFINED encoded text | `area_information_to_exif` |

Satellites and datum require ASCII. Processing method and area accept valid
UTF-8, including supplementary Unicode characters. Source Text values must
use Ascii, Utf8, or Unknown encoding; Unknown is interpreted as UTF-8.
Empty text is an active value, and spaces are retained exactly. NUL, C0/C1
controls, malformed UTF-8, and unsupported source types/encodings fail.
There is no truncation, Unicode normalization, datum conversion, receiver-state
inference, or parsing of satellite lists or processing-method names.

Method and area are stored as UNDEFINED bytes with an eight-byte character-code
identifier. ASCII content uses the ASCII prefix; non-ASCII content uses the
UNICODE prefix followed by a UTF-16LE byte-order marker and exact UTF-16 payload.
Supplementary characters use surrogate pairs. New encoded payloads omit the
optional terminator; ordinary ASCII fields receive their wire terminator during
serialization. Native equivalent text accepts the ASCII prefix or UNICODE with
an explicit LE/BE byte-order marker and at most one trailing terminator. JIS,
unknown identifiers, malformed text, and Unicode without a byte-order marker
are non-equivalent. FailOnConflict protects these native values; PreserveExisting
keeps them, while ReplaceExisting replaces them with canonical new output.
Portable XMP recognizes the same supported encoded forms and emits decoded
text. Unsupported forms are omitted from native-to-XMP projection without
modifying the original native entries.

The v1 method/area version contract accepts GPS 2.2 through 2.4; earlier or
unknown versions fail when an active encoded field is selected. Satellites and
datum retain any well-formed native BYTE[4] version. Missing versions default
to 2.3.0.0, with no upgrade or source-XMP version copy.

Defaults are DirtyOnly/FailOnConflict and four enabled flags. Selection,
singleton conflicts, duplicate repair, dirty tombstone removal, version cleanup,
owned provenance, and source/output aliasing follow the quality GPS contract.
All selected fields commit atomically. Empty strings do not request deletion.
Limits are five added entries including version, 1024 native operations,
4096 source text bytes per selected active property, and 16384 total text bytes.
Encoded output is bounded by twice the source byte limit plus its prefix/BOM;
encoding overhead does not consume the source-text budget. Limits may be lowered.
Preparation may allocate.

Python returns a detached document. Persist it through transfer preparation;
retain source XMP using both `xmp_include_existing=True` and
`xmp_conflict_policy=openmeta.XmpConflictPolicy.ExistingWins`. The five GPS
translation APIs now cover the repository's 32 standard native GPS tag IDs,
including paired companions and automatically maintained GPSVersionID. This
is bounded writeback coverage; it does not imply support for every possible
encoding, version, or arbitrary metadata synchronization policy.

## Conflict And Removal Policy

All translation APIs apply the following behaviors to each complete native
group. Location and editorial translation reuse the descriptive conflict-policy enum.

| Policy | Behavior |
| --- | --- |
| `PreserveExisting` | Keep the complete native group when any member already exists. |
| `FailOnConflict` | Require the group to be absent or already exactly equivalent. This is the default. |
| `ReplaceExisting` | Replace the group, tombstone duplicates, and tombstone stale companion fields. |

A dirty deleted XMP source is propagated only with `ReplaceExisting`; the
corresponding native group is tombstoned. Duplicate eligible XMP source
properties are rejected as ambiguous rather than choosing one occurrence.

The operation is transactional. The source is immutable and the output store
is replaced only after all selected mappings parse, reconcile, and finalize.
New native entries retain the source XMP block and wire provenance, with any
referenced wire-type name copied into output-owned storage.
Date `max_added_entries` and `max_operations` may lower the public hard limits
of 16 added entries and 1024 native operations. Technical translation has
separate hard limits of 6 added entries, 1024 operations, 4096 bytes per text
property, and 16 KiB total source text. Calls keep no global state and are safe
when each concurrent call owns its output store.

Capture translation separately permits at most 5 added entries, 1024
operations, 128 bytes per text property, and 640 total source text bytes. Its
tag-specific conversion always emits scalar `RATIONAL`, `SHORT`, or
`SRATIONAL` values, so generic writer type inference cannot select a different
TIFF type.

Geometry translation permits at most 5 added entries, 1024 operations, 32
bytes per textual property, and 160 total source text bytes. One dirty width or
height member makes the complete active dimension group eligible in
`DirtyOnly` mode. A complete dirty width/height tombstone pair may remove the
native group under `ReplaceExisting` only when the target spec does not still
declare dimensions; orientation follows the same target-contradiction rule.

Descriptive translation separately bounds matched source properties, added
entries, operations, and total text bytes. In `DirtyOnly` mode, one dirty member
makes the complete repeated creator/keyword group eligible, so unchanged active
members are retained while dirty tombstones can remove native values under
`ReplaceExisting`.

## C++ Example

```cpp
#include "openmeta/metadata_transfer.h"
#include "openmeta/metadata_translation.h"

openmeta::MetadataDateTranslationOptions options;
options.conflict_policy
    = openmeta::MetadataDateTranslationConflictPolicy::ReplaceExisting;

openmeta::MetaStore translated;
const openmeta::MetadataDateTranslationResult result
    = openmeta::translate_xmp_creation_dates(edited, options, &translated);
if (result.status != openmeta::MetadataDateTranslationStatus::Ok) {
    // translated is unchanged.
}

openmeta::MetadataTechnicalTranslationOptions technical_options;
technical_options.conflict_policy
    = openmeta::MetadataTechnicalTranslationConflictPolicy::ReplaceExisting;
openmeta::MetaStore technical;
const auto technical_result = openmeta::translate_xmp_technical_metadata(
    translated, technical_options, &technical);

openmeta::MetadataCaptureTranslationOptions capture_options;
capture_options.conflict_policy
    = openmeta::MetadataCaptureTranslationConflictPolicy::ReplaceExisting;
openmeta::MetaStore capture;
const auto capture_result = openmeta::translate_xmp_capture_metadata(
    technical, capture_options, &capture);

openmeta::TransferTargetImageSpec target;
target.has_dimensions = true;
target.width = output_width;
target.height = output_height;
target.has_orientation = true;
target.orientation = output_orientation;
openmeta::MetaStore geometry;
const auto geometry_result = openmeta::translate_xmp_image_geometry(
    capture, target, openmeta::MetadataGeometryTranslationOptions {},
    &geometry);

openmeta::MetadataDescriptiveTranslationOptions descriptive_options;
descriptive_options.conflict_policy
    = openmeta::MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
openmeta::MetaStore descriptive;
const auto descriptive_result = openmeta::translate_xmp_descriptive_metadata(
    geometry, descriptive_options, &descriptive);

openmeta::MetadataLocationTranslationOptions location_options;
location_options.conflict_policy
    = openmeta::MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
openmeta::MetaStore location;
const auto location_result = openmeta::translate_xmp_location_metadata(
    descriptive, location_options, &location);
```

## Python

Python calls the same C++ transaction and returns a detached `Document`:

```python
translated = edited.translate_creation_dates(
    conflict_policy=openmeta.MetadataDateTranslationConflictPolicy.ReplaceExisting,
)
translated = translated.translate_technical_metadata(
    conflict_policy=openmeta.MetadataTechnicalTranslationConflictPolicy.ReplaceExisting,
)
translated = translated.translate_capture_metadata(
    conflict_policy=openmeta.MetadataCaptureTranslationConflictPolicy.ReplaceExisting,
)
translated = translated.translate_image_geometry(target_image_spec)
translated = translated.translate_descriptive_metadata(
    conflict_policy=openmeta.MetadataDescriptiveTranslationConflictPolicy.ReplaceExisting,
)
translated = translated.translate_location_metadata(
    conflict_policy=openmeta.MetadataDescriptiveTranslationConflictPolicy.ReplaceExisting,
)
```

Invalid or lossy requests raise `ValueError` containing the C++ status,
mapping, and source entry ID. The original `Document` is not mutated.

## Scope

This milestone is intentionally limited to exact creation-date, common
technical and capture EXIF, target-bound orientation/stored dimensions, primary
GPS coordinates/time/navigation, combined descriptive/editorial/workflow IPTC, flat IPTC Core locations,
capture settings, and explicit structured-location reconciliation/construction. It does not yet
provide arbitrary EXIF/IPTC/XMP translation, broader target image-layout/storage
projection,
multilingual-alternative selection, timezone inference, numeric approximation
or value repair, or automatic synchronization during transfer.

## Sensitivity companion writeback

`translate_xmp_sensitivity_metadata(...)` and Python
`Document.translate_sensitivity_metadata(...)` use independent
`MetadataSensitivityTranslationOptions`, contract version 1. The seven ExifIFD
fields form one transaction and one result group:

| XMP property in `http://cipa.jp/exif/1.0/` (`exifEX`) | Native tag | Type/range |
| --- | --- | --- |
| PhotographicSensitivity | 0x8827 | SHORT, 1..65535 |
| SensitivityType | 0x8830 | SHORT, 0..7 |
| StandardOutputSensitivity | 0x8831 | LONG, 1..4294967295 |
| RecommendedExposureIndex | 0x8832 | LONG, 1..4294967295 |
| ISOSpeed | 0x8833 | LONG, 1..4294967295 |
| ISOSpeedLatitudeyyy | 0x8834 | LONG, 1..4294967295 |
| ISOSpeedLatitudezzz | 0x8835 | LONG, 1..4294967295 |

Input values are unsigned scalar integers or decimal integer text with optional
leading `+`. Signed scalars, fractions, labels, zero sensitivity values, overflow,
and malformed/indexed/qualified fields fail. This positive-value restriction is
OpenMeta's bounded writeback policy. Exact namespaces are required. Legacy
OpenMeta `exif:ISO`, `exif:ISOSpeedRatings`, and `exif:ISOSpeedRatings[1]` are
base aliases; the six companion names in the older `exif` namespace are also
accepted for historical portable packets. Duplicate properties or aliases fail
even when equal. The existing ISO-only API retains its 1..65535 contract.

An active group requires both base sensitivity and type. Type 0 means unknown;
1 selects SOS, 2 REI, 3 ISO speed, 4 SOS+REI, 5 SOS+ISO, 6 REI+ISO, and 7 all
three. Any supplied selected parameters must equal the base value below 65535,
or use base 65535 for values at or above that limit. Plural selected parameters
must equal each other as full LONG values, including above the SHORT limit.
Unselected parameters may differ. Optional selected parameters may be absent;
a marker without its extended value remains a marker. No type or missing
high-range value is inferred. Latitude values require both latitude fields and
ISOSpeed. EXIF versions and unrelated exposure fields are retained.

Defaults are DirtyOnly/FailOnConflict. One dirty recognized member selects the
whole group, including clean active companions. All selects active sources and
dirty tombstones. No selected source retains the native group. Once selected,
missing or deleted optional sources specify absent target fields. A dirty base
tombstone with no active group members requests complete removal; an orphan
companion tombstone or partial required pair fails. Source XMP entries remain
available for caller-controlled serialization.

PreserveExisting retains the complete native group if any member exists.
FailOnConflict requires the entire group to be absent or exactly equivalent,
including presence, SHORT/LONG types and duplicate counts. ReplaceExisting
updates the group and removes stale/duplicate native members. Validation runs
before all policies, and failures preserve separate or aliased output. Limits
are seven added entries, 1024 operations, 128 text bytes per property and 896
text bytes in total; callers may lower them. Preparation may allocate.

Portable native output now uses `exifEX` for the six companions. A base
with native SensitivityType uses `exifEX:PhotographicSensitivity`; an
ISO-only base retains `exif:ISO`. These seven properties participate in
the existing `CanonicalizeManaged` namespace policy. Complete generated
groups round-trip through this API, including the full unsigned LONG range.
Existing source XMP can be
retained with ExistingWins; independently authored duplicate base aliases still
require caller resolution. Snapshot persistence covers JPEG, Classic TIFF and
BigTIFF creation, replacement and removal.

The wire and companion rules follow the CIPA EXIF 2.32 English translation
(section 4.6.6 and Annex G) and EXIF 2.31 XMP mapping tables. This bounded API
is not a claim of complete current EXIF conformance or automatic reconciliation
with every legacy ISO-array convention.

References: [CIPA EXIF translation](https://cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf),
[CIPA EXIF metadata for XMP](https://cipa.jp/std/documents/e/DC-X010-2017.pdf).

## Camera, lens and spectral text writeback

`translate_xmp_camera_text_metadata(...)` and Python
`Document.translate_camera_text_metadata(...)` use independent
`MetadataCameraTextTranslationOptions` and the existing technical source-mode,
conflict-policy, result and diagnostic enums. Contract version 1 adds six
independent native targets in one transaction.

| Exact XMP property | Native ExifIFD tag | Option |
| --- | --- | --- |
| `exif:SpectralSensitivity` | `0x8824` ASCII | `spectral_sensitivity_to_exif` |
| `exifEX:CameraOwnerName` | `0xa430` ASCII | `camera_owner_name_to_exif` |
| `exifEX:BodySerialNumber` | `0xa431` ASCII | `body_serial_number_to_exif` |
| `exifEX:LensMake` | `0xa433` ASCII | `lens_make_to_exif` |
| `exifEX:LensModel` | `0xa434` ASCII | `lens_model_to_exif` |
| `exifEX:LensSerialNumber` | `0xa435` ASCII | `lens_serial_number_to_exif` |

Here `exif` is `http://ns.adobe.com/exif/1.0/` and `exifEX` is
`http://cipa.jp/exif/1.0/`. The five identity fields also accept the same property
names in the historical OpenMeta `exif` namespace. Prefix spelling is irrelevant;
namespace URI and property path must match. `aux:Lens`, `aux:SerialNumber`,
`OwnerName`, generic `Lens` aliases and MakerNote identity inference are excluded.
The tag types follow [Exif 2.32](https://cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf);
the canonical XMP identity mappings follow
[CIPA metadata interchange guidance](https://cipa.jp/std/documents/e/DC-X010-2017.pdf).

An active value must be nonempty `Text` with `Ascii` or `Utf8` encoding, containing
only printable ASCII bytes `0x20..0x7e`. Leading/trailing spaces, punctuation and
serial-number leading zeros are preserved. XMP decoding retains their boundary
whitespace in description attributes, resource values and element text. Empty values, other value kinds or
encodings fail with `InvalidSourceValue`; NUL, controls, DEL and non-ASCII bytes
fail with `NonAsciiSource`. The bounded printable subset permits lossless
portable XML round trips. No trimming, transliteration, Unicode conversion or
ASTM spectral-curve grammar validation is performed. The latter remains the
caller's responsibility; this API transports the supplied text.

All six switches default to true. `DirtyOnly` selects each dirty property
individually. `All` also selects active clean entries; deleted entries still
require `Dirty`. Missing or disabled properties retain native metadata. Eligible
duplicates, including canonical/legacy alias pairs with equal values, fail with
`AmbiguousSource`. Indexed, qualified or nested forms of a selected property fail
with `UnsupportedSourceShape`. A selected dirty tombstone ignores its payload and
removes only that native field under `ReplaceExisting`.

Each field has its own conflict decision and result group. `FailOnConflict`
rejects a mismatch, `PreserveExisting` retains the conflicting field while other
fields can translate, and `ReplaceExisting` updates the singleton and removes
native duplicates. Native equivalence requires `Text` with `Ascii` or `Utf8`
encoding and matching bytes after native terminal NUL bytes are removed; byte
blobs, numbers and other encodings are conflicts. New values use native ASCII.
All source validation, conflict decisions and resource checks finish before one
commit. Failure leaves separate or aliased output unchanged. Source provenance
and text ownership survive the source store's lifetime.

Limits are 6 added entries, 1024 edit operations, 4096 bytes per property and
24576 total source-text bytes. Limits must be nonzero and at most those ceilings.
At least one switch must be enabled. Existing technical and capture option
layouts and existing enum values are unchanged.

Portable native identity fields now use canonical `exifEX` names. That namespace
is declared only when needed. Spectral sensitivity remains in `exif`.
`CanonicalizeManaged` recognizes all six fields. If callers retain legacy source
XMP along with generated canonical identity properties, both aliases can remain
under ordinary preservation policies; reverse translation rejects their
ambiguity. Use `CanonicalizeManaged` when replacing those managed aliases.

Shared tests cover all six fields, conflict and removal transactions, source
shapes and budgets, portable XML and namespace policies, serialized snapshots
through JPEG/Classic TIFF/BigTIFF, the Python wrapper and an installed shared
library consumer. Related fields are implemented and reviewed together, with
focused checks during development and one final platform matrix per stable
batch. Lens specification and image identity use the separate contract below.
APEX is covered by the combined batch below; focal-plane/subject arrays remain later work.

## Lens specification and image identity writeback

`translate_xmp_identity_metadata(...)` and Python
`Document.translate_identity_metadata(...)` use
`MetadataIdentityTranslationOptions`, contract version 1, and the existing
capture source modes, conflict policies, statuses and result counters. The two
fields have independent switches and conflict decisions, followed by one commit.

| Source | Native ExifIFD field | Option |
| --- | --- | --- |
| `exifEX:LensSpecification` | `0xA432`, four unsigned RATIONALs | `lens_specification_to_exif` |
| `exif:ImageUniqueID` | `0xA420`, ASCII, 33 bytes on wire | `image_unique_id_to_exif` |

Lens specification also accepts the historical OpenMeta `exif` namespace alias.
The four ordered components are minimum focal length, maximum focal length,
minimum F-number at the minimum focal length, and minimum F-number at the maximum
focal length. Focal lengths must be positive, with minimum <= maximum. Apertures
must be positive or exactly `0/0`, the EXIF unknown-aperture marker. The API does
not infer missing values, constrain one aperture relative to the other, or parse
lens model descriptions.

Accepted lens shapes are one `URational` array of exactly four elements, or four
scalar properties named `LensSpecification[1]` through `[4]`. Indexed values
accept unsigned integer/rational scalars or ASCII/UTF-8 numeric text: integer,
decimal, scientific notation or `numerator/denominator`. Conversion is exact and
reduced to unsigned 32-bit components; unrepresentable values fail. Floating
point scalars, units, opaque lists, sparse/noncanonical indexes, nested fields,
root/index mixtures and mixed or duplicate namespace aliases fail. The store
retains flattened indexes, not the original RDF Seq/Bag container kind; hosts
must supply the four components in the documented order. Portable output uses a
standard `exifEX` RDF Seq with exact fraction text and preserves `0/0`.

Under `DirtyOnly`, any dirty lens member selects the whole lens group, including
clean companions. A root tombstone must stand alone. Four dirty indexed
tombstones also remove the group; partial deletion fails. Missing sources and
clean tombstones do nothing. `All` selects clean active sources too. Image ID
selection is independent and accepts only the exact scalar property.

Image IDs require exactly 32 hexadecimal ASCII characters (`0-9`, `a-f`, `A-F`),
stored as ASCII/UTF-8 Text. No trimming, case folding, hyphen removal or ID
creation occurs. Leading zeros and letter case survive. A native equivalent must
be ASCII/UTF-8 Text with those same bytes, optionally followed by one NUL. The
serializer writes one terminator for the required 33-byte wire representation.
Generic validation and validated authoring allow `0/0` only in the exact
unsigned lens aperture slots (native array, XMP root array or scalar `[3]`/`[4]`).
Other zero-denominator values remain errors. The translation contract also checks
positive values, focal ordering and completeness. Native lens equivalence
requires a four-element URational array with equal
rational values and exact unknown markers; equivalent unreduced values remain.
Malformed native types or duplicate native tags are conflicts.

`FailOnConflict` rejects differences, `PreserveExisting` retains existing native
fields, and `ReplaceExisting` applies updates, duplicate repair and requested
removals. Every source check and budget check precedes mutation of either field.
Failure retains both a separate output and an aliased input/output unchanged.
Preparation and commit may allocate; synchronization of shared objects is the
host's responsibility. The API has no internal synchronization or ID allocator.

Limits are 2 added native entries, 1024 edit operations, 128 text bytes per source
member, and 544 total text bytes (four lens members plus a 32-byte image ID).
Callers can lower these positive limits. Typed lens arrays are fixed at 32 bytes
and do not consume the text budget. Source properties count actual selected
entries; successful translations count at most two groups.

Portable native output skips malformed lens/ID values. `CanonicalizeManaged`
recognizes the canonical lens array and removes managed source copies before
native projection. Preservation policies may retain a historical alias alongside
a generated canonical value, which reverse translation rejects as ambiguous.
XMP decoding preserves boundary whitespace for `exif:ImageUniqueID`, so invalid
padded IDs are rejected consistently for attributes, resources and element text.

The structural contract follows the
[Exif 2.3 field definitions](https://www.cipa.jp/std/documents/e/DC-008-2012_E.pdf)
and [CIPA Exif/XMP mappings](https://cipa.jp/std/documents/e/DC-X010-2017.pdf).
It does not validate UUID generation/version, global uniqueness, or an
application's capture-time identity retention policy. Hosts decide whether an
explicit replacement or removal is appropriate for their workflow and Exif
version. No existing EXIF version is upgraded implicitly.

Combined tests cover C++, Python, installed shared consumers, exact portable
round trips, and serialized snapshots through JPEG, Classic TIFF and BigTIFF.
The APEX batch below extends this coverage; focal-plane/subject fields remain.

## APEX writeback (contract version 1)

`translate_xmp_apex_metadata(source, MetadataApexTranslationOptions{}, &output)`
writes five scalar properties in one transaction. Python exposes the same
contract as `Document.translate_apex_metadata(...)`. The result, source modes,
conflict policies and diagnostics use the existing capture types.

| Exact source in `http://ns.adobe.com/exif/1.0/` | ExifIFD target | Native type | Independent option |
| --- | --- | --- | --- |
| `ShutterSpeedValue` | `0x9201` | `SRATIONAL`, count 1 | `shutter_speed_value_to_exif` |
| `ApertureValue` | `0x9202` | `RATIONAL`, count 1 | `aperture_value_to_exif` |
| `BrightnessValue` | `0x9203` | `SRATIONAL`, count 1 | `brightness_value_to_exif` |
| `ExposureBiasValue` or `ExposureCompensation` | `0x9204` | `SRATIONAL`, count 1 | `exposure_bias_value_to_exif` |
| `MaxApertureValue` | `0x9205` | `RATIONAL`, count 1 | `max_aperture_value_to_exif` |

All inputs are direct APEX values. There is no conversion from seconds or
f-numbers, inference from ExposureTime/FNumber, exposure-equation check, version
upgrade, or consistency check between the five fields. The two aperture fields
accept zero and positive values. The signed fields accept either sign. The
usual brightness/bias range of -99.99 to 99.99 is not a hard limit.

The structural types and brightness sentinel follow
[CIPA DC-008-2012, camera information tags and Annex C](https://www.cipa.jp/std/documents/e/DC-008-2012_E.pdf).
The XMP property names follow
[CIPA DC-X010-2017, Exif mappings](https://cipa.jp/std/documents/e/DC-X010-2017.pdf).
This contract does not claim full conformance to every tag or later Exif revision.

### Exact numbers and unknown brightness

Sources may be scalar signed/unsigned integers, the target rational type, or
ASCII/UTF-8/Unknown-encoding text containing an integer, decimal, scientific
number or `numerator/denominator`. Signed integers for aperture fields must be
nonnegative. Signed rational fields require `SRational`, and unsigned rational
fields require `URational`. Floating-point, arrays, structured children,
qualifiers, units, whitespace in numeric text and zero/negative denominators
fail. XMP decoding retains its existing whitespace normalization; this batch
does not change reader behavior. Exact fractions reduce to 32-bit components;
numeric text parsing uses bounded 64-bit intermediates. Overflow fails without
approximation. Denominators are positive, at most `INT32_MAX` for signed fields
or `UINT32_MAX` for unsigned fields.

Brightness reserves a **wire numerator of -1** (`0xffffffff`) for unknown,
regardless of its positive denominator. Explicit fraction text `-1/n`, a typed
`SRational{-1, n}`, or exact text `Unknown` selects this sentinel before
reduction and writes `-1/1`. Sentinel fraction denominators must fit `INT32_MAX`.
Integer and decimal/scientific inputs are finite: `-1`, `-1.0` and a signed
integer -1 write `-2/2`. A finite `-0.5` or explicit `-2/4` writes `-2/4`.
Reduced finite fractions with numerator -1 use `-2/(2n)`; if the doubled
denominator cannot fit, translation returns `ValueOutOfRange`. Unknown and
finite native brightness values never compare equal during conflict handling.
Positive-denominator native unknown fractions compare equivalent to each other.

### Selection, conflicts and bounds

All five switches default to true. At least one must be enabled. DirtyOnly
selects eligible dirty scalar sources; All also selects active clean sources.
Deleted sources require Dirty in either mode. Missing sources retain native
values. Each field reconciles independently with PreserveExisting,
FailOnConflict or ReplaceExisting, and duplicate eligible aliases fail even
when their values agree. Indexed/structured shapes of enabled properties fail.
Unrelated names, namespaces and disabled mappings are ignored.

Native equivalence requires the correct scalar type/count and equal rational
value, with the brightness sentinel rule above. ReplaceExisting repairs wrong
types and duplicate native entries. A dirty tombstone removes all native
instances under ReplaceExisting. Parsing, conflicts and shared limits complete
before one commit. Failure preserves both separate and aliased output; success
owns its values and provenance. Preparation may allocate. The host synchronizes
conflicting access to shared stores.

Default and hard maximum budgets are five added entries, 1024 edit operations,
128 bytes per text property and 640 total text bytes. Duplicate repair and
removal consume the operation budget. Lower positive caller limits are allowed.
The exposure-bias mapping and its diagnostic `XmpExposureCompensation` are
shared with the older capture API; that API's options and input behavior remain
unchanged.

### Portable output change in 0.5.3

Generated portable XMP now emits exact fractions for all five native APEX tags.
For example, native shutter `6/1` emits `ShutterSpeedValue=6/1`; native aperture
`4/1` emits `ApertureValue=4/1`. Previous releases emitted seconds (`1/64`) and
f-numbers (`4.0`) under these names. Old generated packets cannot be identified
reliably from their values; regenerate them from native EXIF before reverse
translation. No heuristic migration is performed.

Brightness and exposure compensation also use fractions instead of rounded
floating-point text. Existing correctly typed scalar XMP APEX rationals also
retain their exact wire fractions, including noncanonical unknown brightness. The existing portable `ExposureCompensation` alias remains.
Unknown brightness emits `-1/1`; finite wire fractions retain their numerator
so the sentinel distinction survives. Wrong native types/counts and nonpositive
denominators are omitted. Large representable APEX values remain exact without
exponentiation or a physical aperture limit. Default existing-XMP precedence
remains unchanged; CanonicalizeManaged permits valid native values to replace
managed source properties. Host FlatHost/Spec projections are separate and
unchanged.

The combined batch adds four native targets, bringing capture-related coverage
to 41 targets across nine APIs. The next core batch is focal-plane/subject
contracts. OIIO/iRAW application acceptance remains a separate downstream task.
