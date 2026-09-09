Metadata Translation
====================

``openmeta/metadata_translation.h`` provides bounded explicit projection
between metadata families. Current contracts translate edited XMP creation
dates into native EXIF/IPTC date groups, exact technical XMP/TIFF properties
into native EXIF fields, typed capture properties into native EXIF scalars,
target-bound image geometry into native TIFF/EXIF groups, and exact descriptive,
flat location, and editorial XMP properties into native IPTC-IIM datasets before
transfer or writing.

The APIs are experimental and versioned by
``kMetadataDateTranslationContractVersion == 1`` and
``kMetadataTechnicalTranslationContractVersion == 1`` and
``kMetadataCaptureTranslationContractVersion == 1`` and
``kMetadataGeometryTranslationContractVersion == 1`` and
``kMetadataDescriptiveTranslationContractVersion == 1`` and
``kMetadataLocationTranslationContractVersion == 1`` and
``kMetadataEditorialTranslationContractVersion == 1`` and
``kMetadataIptcTranslationContractVersion == 1`` and
``kMetadataGpsTranslationContractVersion == 1`` and
``kMetadataStructuredLocationTranslationContractVersion == 1``.

Workflow
--------

Translation is a separate step. Creation, editing, transfer, and writing do not
invoke it implicitly:

1. Read, create, or edit a finalized ``MetaStore``.
2. Call ``translate_xmp_creation_dates(...)``,
   ``translate_xmp_technical_metadata(...)``,
   ``translate_xmp_capture_metadata(...)``,
   ``translate_xmp_image_geometry(...)``,
   ``translate_xmp_descriptive_metadata(...)``,
   ``translate_xmp_location_metadata(...)``,
   ``translate_xmp_editorial_metadata(...)``,
   ``translate_xmp_iptc_metadata(...)``, ``translate_xmp_gps_metadata(...)``,
   ``translate_xmp_structured_location_metadata(...)``,
   or the required combination
   with explicit mapping and conflict options.
3. Pass the returned finalized store to transfer preparation or a writer.

This separation prevents a transfer from unexpectedly replacing native camera
dates merely because decoded XMP is present. The default source mode is
``DirtyOnly``, so only caller-modified XMP values and tombstones are eligible.

Date mappings
-------------

.. list-table::
   :header-rows: 1
   :widths: 22 38 40

   * - XMP source
     - Native destination
     - Precision requirements
   * - ``xmp:CreateDate``
     - EXIF ``DateTimeDigitized``, ``OffsetTimeDigitized``, and
       ``SubSecTimeDigitized``
     - Time is required. Timezone and up to nine fractional digits are
       preserved in companion tags.
   * - ``xmp:CreateDate``
     - IPTC ``DigitalCreationDate`` and ``DigitalCreationTime``
     - Date-only or whole seconds are accepted. Fractional seconds are
       rejected.
   * - ``photoshop:DateCreated``
     - IPTC ``DateCreated`` and ``TimeCreated``
     - Date-only or whole seconds are accepted. Fractional seconds are
       rejected.
   * - XMP ``exif:DateTimeOriginal``
     - EXIF ``DateTimeOriginal``, ``OffsetTimeOriginal``, and
       ``SubSecTimeOriginal``
     - Time is required. Timezone and up to nine fractional digits are
       preserved in companion tags.

Accepted values use a full Gregorian ``YYYY-MM-DD`` date, optionally followed
by ``T`` and ``hh:mm:ss``, up to nine fractional digits, and ``Z`` or a
``+/-HH:MM`` timezone. Invalid dates, malformed values, and conversions that
would discard precision fail instead of being normalized or truncated.
Lexical ``-00:00`` is preserved as a negative-zero offset.

Each mapping can be disabled independently. For example, callers that need to
retain fractional ``xmp:CreateDate`` can disable its IPTC projection while
keeping exact EXIF projection.

Technical EXIF mappings
-----------------------

.. list-table::
   :header-rows: 1
   :widths: 24 38 38

   * - XMP source
     - Native EXIF destination
     - Requirements
   * - ``xmp:ModifyDate``
     - IFD0 ``DateTime`` plus ExifIFD ``OffsetTime`` and ``SubSecTime``
     - Time is required. Timezone and up to nine fractional digits are
       preserved in companion tags.
   * - ``tiff:Make``
     - IFD0 ``Make``
     - Non-empty 7-bit ASCII without embedded NUL bytes.
   * - ``tiff:Model``
     - IFD0 ``Model``
     - Non-empty 7-bit ASCII without embedded NUL bytes.
   * - ``xmp:CreatorTool``
     - IFD0 ``Software``
     - Non-empty 7-bit ASCII without embedded NUL bytes.

Namespaces and property paths must match exactly. These mappings are intended
for edited or newly created host metadata, not for copying source-bound camera
processing data. Each singleton and the complete ``ModifyDate`` companion
group reconcile independently, so a conflict in ``Make`` does not silently
change ``Model``.

Capture EXIF mappings
---------------------

.. list-table::
   :header-rows: 1
   :widths: 34 38 28

   * - XMP source
     - Native EXIF destination
     - Required native type
   * - ``exif:ExposureTime``
     - ExifIFD ``ExposureTime``
     - One unsigned ``RATIONAL``, greater than zero
   * - ``exif:FNumber``
     - ExifIFD ``FNumber``
     - One unsigned ``RATIONAL``, greater than zero
   * - ``exif:ISO``, ``exif:ISOSpeedRatings``, or
       ``exif:ISOSpeedRatings[1]``
     - ExifIFD ``ISOSpeedRatings``
     - One ``SHORT`` in ``1..65535``
   * - ``exif:FocalLength``
     - ExifIFD ``FocalLength``
     - One unsigned ``RATIONAL``, greater than zero
   * - ``exif:ExposureCompensation`` or ``exif:ExposureBiasValue``
     - ExifIFD ``ExposureBiasValue``
     - One signed ``SRATIONAL``

Rational sources may be typed scalar XMP values or full text integers,
decimals, scientific decimals, and ``numerator/denominator`` values. Focal
length also accepts the OpenMeta portable `` mm`` suffix. Conversion uses
integer arithmetic and reduces the exact source value before checking the
32-bit EXIF numerator and denominator limits. It never uses a floating-point
approximation. For example, ``2.8`` becomes ``14/5`` and ``8e-3`` becomes
``1/125``.

This exactness is intentionally strict. A bounded repeating decimal such as
``0.333333333333333`` does not fit native ``SRATIONAL`` exactly and returns
``ValueOutOfRange``; provide ``1/3`` or a typed signed rational when exact
thirds are required. ISO rejects multi-value arrays, decimals, zero, and values
above ``65535`` instead of selecting, truncating, or changing the native TIFF
type.

Portable and standard aliases target the same native singleton. If more than
one eligible alias is present, the source is ambiguous and translation fails
rather than selecting one.

Target-bound image geometry
---------------------------

``translate_xmp_image_geometry(...)`` projects ``tiff:Orientation`` and
complete XMP width/height pairs only when they agree with a caller-provided
``TransferTargetImageSpec``. Orientation becomes one IFD0 ``SHORT``. Stored
dimensions become IFD0 ``ImageWidth``/``ImageLength`` and ExifIFD
``PixelXDimension``/``PixelYDimension`` ``LONG`` values.

Width aliases are ``tiff:ImageWidth``, ``exif:ExifImageWidth``, and
``exif:PixelXDimension``. Height aliases are ``tiff:ImageLength``, portable
``tiff:ImageHeight``, ``exif:ExifImageHeight``, and
``exif:PixelYDimension``. Aliases may coexist only when all values agree.

Dimensions describe the stored raster and are never swapped for orientation
indices 5 through 8. Missing target facts, target mismatches, contradictory
aliases, incomplete pairs, and mixed active/deleted pairs fail transactionally.

Descriptive mappings
--------------------

.. list-table::
   :header-rows: 1
   :widths: 34 46 20

   * - XMP source
     - Native IPTC-IIM destination
     - Maximum encoded bytes
   * - ``dc:title[@xml:lang=x-default]``
     - ``ObjectName`` (2:5)
     - 64
   * - ``dc:description[@xml:lang=x-default]``
     - ``Caption-Abstract`` (2:120)
     - 2000
   * - ``dc:creator[n]``
     - repeated ``By-line`` (2:80)
     - 32 per value
   * - ``dc:subject[n]``
     - repeated ``Keywords`` (2:25)
     - 64 per value
   * - ``dc:rights[@xml:lang=x-default]``
     - ``CopyrightNotice`` (2:116)
     - 128
   * - ``photoshop:Credit``
     - ``Credit`` (2:110)
     - 32
   * - ``photoshop:Source``
     - ``Source`` (2:115)
     - 32

The default-language and indexed paths must match exactly. Creator and keyword
items retain numeric XMP index order. Duplicate singleton properties or
duplicate active indexes are ambiguous and fail rather than selecting a value.
IPTC-IIM limits are byte limits; text is never truncated.

Non-ASCII values remain UTF-8. Translation emits IPTC ``CodedCharacterSet``
(1:90) with ``ESC % G`` when the marker is absent and every existing active
IPTC value is ASCII or will be replaced by the same transaction. An
incompatible charset marker or unrelated legacy high-bit data returns
``NativeEncodingConflict`` without modifying the output.

Location mappings
-----------------

``translate_xmp_location_metadata(...)`` accepts
``MetadataLocationTranslationOptions`` and returns
``MetadataDescriptiveTranslationResult``. It uses the descriptive source modes,
conflict policies, statuses, and mapping diagnostics. The existing descriptive
API still selects only its original seven mappings.

.. list-table::
   :header-rows: 1
   :widths: 40 45 15

   * - Exact XMP source
     - Native IPTC-IIM destination
     - Maximum bytes
   * - ``photoshop:City``
     - ``City`` (2:90)
     - 32
   * - ``Iptc4xmpCore:Location``
     - ``Sub-location`` (2:92)
     - 32
   * - ``photoshop:State``
     - ``Province-State`` (2:95)
     - 32
   * - ``photoshop:Country``
     - ``Country-PrimaryLocationName`` (2:101)
     - 64
   * - ``Iptc4xmpCore:CountryCode``
     - ``Country-PrimaryLocationCode`` (2:100)
     - 3

These are the flat legacy location mappings in the
`IPTC Photo Metadata Standard <https://www.iptc.org/std/photometadata/specification/IPTC-PhotoMetadata-2025.1.html>`_.
Each mapping is an independent singleton. Namespace URIs and property paths
must match exactly; duplicate active sources are rejected even when equal.
Empty values, invalid UTF-8/XML text, embedded NULs, and excess bytes fail
without changing the output. Removal uses a dirty tombstone.

Country codes require exactly two or three uppercase ASCII letters and are
preserved byte for byte. Code membership, conversion between two and three
letters, and agreement with the country name belong to the caller. Structured
``LocationCreated``/``LocationShown`` properties, indexed or qualified forms,
and GPS are not aliases and are not selected by this API. The operation does
not infer whether a flat location describes the camera or the depicted subject.

UTF-8 charset promotion follows the descriptive policy above, including
rejection of incompatible markers and unrelated legacy high-bit IPTC values.
Limits are 1024 matched source properties, 4096 operations, 8 MiB of inspected
text/charset-safety bytes, and six added entries (five datasets plus one charset
marker). Options can lower these limits. New entries copy source provenance;
updates preserve the existing native entry's provenance. Translation is a
preparation operation that may allocate; it is not an allocation-free replay API.

Structured location reconciliation
----------------------------------

``translate_xmp_structured_location_metadata(...)`` accepts
``MetadataStructuredLocationTranslationOptions`` and returns the shared
``MetadataDescriptiveTranslationResult``. Contract version 1 selects one
structured location and reconciles its five supported text fields into **both
flat legacy XMP and native IPTC-IIM**. Existing flat-location and combined IPTC
APIs keep their original mapping sets.

The root namespace must be ``http://iptc.org/std/Iptc4xmpExt/2008-02-29/``.
``location_kind`` defaults to ``MetadataStructuredLocationKind::Shown``;
``Created`` requires an explicit choice. Shown and Created never fall back to
each other. These properties distinguish the depicted place from the place
where the camera was. Choosing Created authorizes copying that camera-location
description into the legacy fields whose historical semantics are ambiguous.
See the `IPTC Photo Metadata specification <https://www.iptc.org/std/photometadata/specification/IPTC-PhotoMetadata-2025.1.html>`_.

.. list-table::
   :header-rows: 1

   * - Structured child
     - Flat XMP destination
     - Native IPTC
     - Maximum UTF-8 bytes
   * - ``City``
     - ``photoshop:City``
     - 2:90
     - 32
   * - ``Sublocation``
     - ``Iptc4xmpCore:Location``
     - 2:92
     - 32
   * - ``ProvinceState``
     - ``photoshop:State``
     - 2:95
     - 32
   * - ``CountryName``
     - ``photoshop:Country``
     - 2:101
     - 64
   * - ``CountryCode``
     - ``Iptc4xmpCore:CountryCode``
     - 2:100
     - 3


Paths are ``LocationShown[n]/City``, ``LocationCreated[n]/City``, and the equivalent
paths for the other four children. The existing scalar ``LocationCreated/City``
resource form is accepted as record index 1. Mixed scalar and indexed Created
forms fail. Indexes must be positive decimal uint32 values without leading
zeros. ``location_index == 0`` selects the sole represented record when present;
an absent root kind is a no-op, and multiple records fail with AmbiguousLocation.
A positive option selects that exact index
and fails with LocationNotFound if absent. It never selects the first record
implicitly or merges records. Any represented child, including an unsupported
child, counts for record selection. Selection precedes DirtyOnly filtering.

XML decoding uses one-based RDF order; authored stores can have sparse indexes.
Canonical unqualified child names and ``Iptc4xmpExt:``-qualified equivalents are
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
``NativeConflict`` covers either destination family.

The source stays immutable. Failure leaves the output unchanged; output/source
aliasing is supported. New entries own copied source provenance, while updates
retain existing destination provenance. Preparation may allocate.

.. code-block:: python

   translated = document.translate_structured_location_metadata(
       location_kind=openmeta.MetadataStructuredLocationKind.Shown,
       location_index=2,
       source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
       conflict_policy=openmeta.MetadataDescriptiveTranslationConflictPolicy.ReplaceExisting,
   )


The returned document is detached. Persist both XMP and IPTC from its snapshot
to keep both destinations synchronized. For Python ``transfer_snapshot_file``,
set ``xmp_include_existing=True``: that transfer API defaults to projecting native
metadata only. This option retains the reconciled flat XMP and structured
records. GPS translation is a separate explicit contract; structured location
coordinates are not aliases for primary EXIF GPS.

Editorial mappings
------------------

``translate_xmp_editorial_metadata(...)`` accepts
``MetadataEditorialTranslationOptions`` and returns
``MetadataDescriptiveTranslationResult``. Each mapping has an independent flag;
``DirtyOnly`` and ``FailOnConflict`` are the defaults. Descriptive and location
calls retain their existing mapping sets.

.. list-table::
   :header-rows: 1
   :widths: 40 45 15

   * - Exact XMP source
     - Native IPTC-IIM destination
     - Maximum encoded bytes
   * - ``photoshop:Headline``
     - ``Headline`` (2:105)
     - 256
   * - ``photoshop:Instructions``
     - ``SpecialInstructions`` (2:40)
     - 256
   * - ``photoshop:TransmissionReference``
     - ``OriginalTransmissionReference`` (2:103)
     - 32

The mappings and limits follow the
IPTC Photo Metadata Standard 2025.1 (see the location specification link above).
Sources must be exact singleton properties in the Photoshop namespace
``http://ns.adobe.com/photoshop/1.0/``. Title, description, rights usage terms,
and other job identifiers are not aliases. Empty text, invalid UTF-8/XML,
embedded NULs, duplicate active singletons, and excess encoded bytes fail
transactionally. Values are never truncated. Dirty tombstones request removal
under ``ReplaceExisting``.

The shared IPTC transaction enforces charset safety, preserves unrelated
native data, copies source provenance for new entries, and retains native
provenance for updates. Limits are 1024 matched source properties, four added
entries (three datasets plus one UTF-8 charset marker), 4096 operations, and
8 MiB of inspected text/charset-safety bytes. Options may lower these limits;
disabling every mapping is invalid. This preparation operation may allocate.

Python exposes the same operation as ``Document.translate_editorial_metadata``,
with ``headline_to_iptc``, ``instructions_to_iptc``, and
``transmission_reference_to_iptc`` flags. It returns a detached document and raises
``ValueError`` with mapping diagnostics on failure. For clean metadata read from
a file, explicitly select ``MetadataDescriptiveTranslationSourceMode.All``.

Combined IPTC writeback
-----------------------

``translate_xmp_iptc_metadata(...)`` accepts ``MetadataIptcTranslationOptions``
and returns ``MetadataDescriptiveTranslationResult``. It selects the seven
descriptive, five location, and three editorial groups above, plus the five
workflow mappings below. All 20 text/priority groups share one transaction,
including charset promotion and resource accounting. The existing subgroup
APIs retain their original options and mappings. Date/time fields remain in
``translate_xmp_creation_dates(...)``; composing the two calls does not make
them one transaction.

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - Exact Photoshop XMP source
     - Native IPTC-IIM destination
     - Contract
   * - ``AuthorsPosition``
     - By-lineTitle (2:85)
     - Singleton text, at most 32 UTF-8 bytes
   * - ``CaptionWriter``
     - Writer-Editor (2:122)
     - Singleton text, at most 32 UTF-8 bytes
   * - ``Category``
     - Category (2:15)
     - One to three ASCII letters, case preserved
   * - ``SupplementalCategories[n]``
     - Repeated SupplementalCategories (2:20)
     - Text, at most 32 UTF-8 bytes per value
   * - ``Urgency``
     - Urgency (2:10)
     - One text digit from 1 to 8, or a signed/unsigned integer scalar in that range

These contracts use the
`Adobe Photoshop XMP namespace <https://developer.adobe.com/xmp/docs/xmp-namespaces/photoshop/>`__,
`IPTC IIM 4.2 <https://www.iptc.org/std/IIM/4.2/specification/IIMV4.2.pdf>`__, and
`IPTC Photo Metadata 2025.1 <https://www.iptc.org/std/photometadata/specification/IPTC-PhotoMetadata-2025.1.html>`__.
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
integer. Values ``0`` and ``9``, floats, rationals, arrays, and alternative textual
spellings such as ``05``, ``+5``, or ``5.0`` are rejected. IPTC-to-portable-XMP
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

Python exposes ``Document.translate_iptc_metadata(...)`` with the same flags,
policies, limits, detached output, and ValueError diagnostics. For example:

.. code-block:: python

   translated = document.translate_iptc_metadata(
       source_mode=openmeta.MetadataDescriptiveTranslationSourceMode.All,
       conflict_policy=openmeta.MetadataDescriptiveTranslationConflictPolicy.ReplaceExisting,
       category_to_iptc=False,
       urgency_to_iptc=False,
   )


The combined API and existing date API cover the 24 active IPTC destination
assignments in ExifTool's
`xmp2iptc.args inventory <https://raw.githubusercontent.com/exiftool/exiftool/master/arg_files/xmp2iptc.args>`__
checked on 2026-09-09. This is a field inventory, not semantic or behavioral
parity: the two commented taxonomy mappings, Photoshop IPTCDigest, arbitrary
IPTC datasets, and ExifTool's conversion/overwrite conventions are excluded.

Primary GPS writeback
---------------------

``translate_xmp_gps_metadata(...)`` accepts ``MetadataGpsTranslationOptions``
and returns ``MetadataGpsTranslationResult``. Contract version 1 covers three
primary EXIF GPS groups, each with an independent flag. It does not select
structured IPTC locations, destination coordinates, GPS time, navigation data,
or nonstandard standalone XMP latitude/longitude reference properties.

.. list-table:: Primary GPS mappings
   :header-rows: 1
   :widths: 40 40 20

   * - Exact EXIF XMP source
     - Native gpsifd fields
     - Flag
   * - ``GPSLatitude``
     - LatitudeRef (1), Latitude (2)
     - ``latitude_to_exif``
   * - ``GPSLongitude``
     - LongitudeRef (3), Longitude (4)
     - ``longitude_to_exif``
   * - ``GPSAltitude`` and ``GPSAltitudeRef``
     - AltitudeRef (5), Altitude (6)
     - ``altitude_to_exif``

Coordinates accept unsigned integer degrees followed by decimal minutes, or
integer minutes and decimal seconds: ``35,48.125N`` or ``139,34,55.25W``.
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
exact unsigned text (``123.45``, ``2469/20``, or ``0``). Floats, signed text, exponent
notation, units, arrays, and zero denominators are rejected. A separate
``GPSAltitudeRef`` is mandatory. This contract uses the legacy Adobe XMP sea-level
convention: text ``0``/``1``, or an integer scalar 0/1. It does not infer a missing
reference from the sign or from existing native metadata.

GPSVersionID is structural companion metadata. A missing version becomes BYTE[4]
``2.3.0.0`` when a selected active result needs it. An existing version must be one
well-formed BYTE[4] entry; malformed or duplicate active versions fail. Existing
version bytes are retained. Altitude supports ``2.0.0.0`` through ``2.4.0.0``:
legacy versions use native sea-level references 0/1; version 2.4 uses 2/3 for the
same meaning. Unknown versions fail altitude translation. No geoid, ellipsoid,
horizontal datum, or unit conversion is performed, and native GPSVersionID is
not upgraded. The XMP GPSVersionID property is not copied or used to infer an
altitude convention. Callers with modern ellipsoidal-height XMP must not pass
it under this legacy sea-level contract.

Coordinate syntax and legacy XMP altitude semantics follow the
`Adobe EXIF namespace <https://developer.adobe.com/xmp/docs/xmp-namespaces/exif/>`__
and `CIPA's EXIF/XMP mapping <https://cipa.jp/std/documents/e/DC-X010-2017.pdf>`__.
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

.. code-block:: python

   translated = document.translate_gps_metadata(
       source_mode=openmeta.MetadataGpsTranslationSourceMode.All,
       conflict_policy=openmeta.MetadataGpsTranslationConflictPolicy.ReplaceExisting,
   )


Pass the detached result to transfer preparation to persist it. Portable XMP
now writes all four GPSVersionID components. Its existing coordinate/altitude
formatters can round native rational values; this reverse API cannot recover
precision already lost before the XMP source was created. This contract covers
primary position writeback, not all GPS metadata or complete ExifTool parity.

Conflict and removal policy
---------------------------

All translation APIs apply the following behaviors to each complete native
group. Location and editorial translation reuse the descriptive conflict-policy enum.

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Policy
     - Behavior
   * - ``PreserveExisting``
     - Keep the complete native group when any member already exists.
   * - ``FailOnConflict``
     - Require the group to be absent or already exactly equivalent. This is
       the default.
   * - ``ReplaceExisting``
     - Replace the group, tombstone duplicates, and tombstone stale companion
       fields.

A dirty deleted XMP source is propagated only with ``ReplaceExisting``; the
corresponding native group is tombstoned. Duplicate eligible XMP source
properties are rejected as ambiguous rather than choosing one occurrence.

The operation is transactional. The source is immutable and the output store
is replaced only after all selected mappings parse, reconcile, and finalize.
New native entries retain the source XMP block and wire provenance, with any
referenced wire-type name copied into output-owned storage.
Date ``max_added_entries`` and ``max_operations`` may lower the public hard
limits of 16 added entries and 1024 native operations. Technical translation
has separate hard limits of 6 added entries, 1024 operations, 4096 bytes per
text property, and 16 KiB total source text. Calls keep no global state and are
safe when each concurrent call owns its output store.

Capture translation separately permits at most 5 added entries, 1024
operations, 128 bytes per text property, and 640 total source text bytes. Its
tag-specific conversion always emits scalar ``RATIONAL``, ``SHORT``, or
``SRATIONAL`` values, so generic writer type inference cannot select a
different TIFF type.

Geometry translation permits at most 5 added entries, 1024 operations, 32
bytes per text property, and 160 total source text bytes. One dirty dimension
makes the complete active pair eligible. Dirty geometry tombstones may remove
native fields under ``ReplaceExisting`` only when the target spec does not
still declare the same fact.

Descriptive translation separately bounds matched source properties, added
entries, operations, and total text bytes. In ``DirtyOnly`` mode, one dirty
member makes the complete repeated creator/keyword group eligible, so
unchanged active members are retained while dirty tombstones can remove native
values under ``ReplaceExisting``.

C++ example
-----------

.. code-block:: cpp

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
   const auto descriptive_result
       = openmeta::translate_xmp_descriptive_metadata(
           geometry, descriptive_options, &descriptive);

   openmeta::MetadataLocationTranslationOptions location_options;
   location_options.conflict_policy
       = openmeta::MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
   openmeta::MetaStore location;
   const auto location_result = openmeta::translate_xmp_location_metadata(
       descriptive, location_options, &location);

Python
------

Python calls the same C++ transaction and returns a detached ``Document``:

.. code-block:: python

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

Invalid or lossy requests raise ``ValueError`` containing the C++ status,
mapping, and source entry ID. The original ``Document`` is not mutated.

Scope
-----

This milestone is intentionally limited to exact creation-date, common
technical and capture EXIF, target-bound orientation/stored dimensions, primary
GPS, combined descriptive/editorial/workflow IPTC, flat IPTC Core locations,
and selected structured-location reconciliation. It does not yet
provide arbitrary EXIF/IPTC/XMP translation, broader target layout/storage
projection, multilingual-alternative selection, timezone inference, numeric approximation or value
repair, or automatic synchronization during transfer.
