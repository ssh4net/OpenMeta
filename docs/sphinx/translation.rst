Metadata Translation
====================

``openmeta/metadata_translation.h`` provides bounded explicit projection
between metadata families. Current contracts translate edited XMP creation
dates into native EXIF/IPTC date groups, exact technical XMP/TIFF properties
into native EXIF fields, typed capture properties into native EXIF scalars,
target-bound image geometry into native TIFF/EXIF groups, and exact descriptive
XMP properties into native IPTC-IIM datasets before transfer or writing.

The APIs are experimental and versioned by
``kMetadataDateTranslationContractVersion == 1`` and
``kMetadataTechnicalTranslationContractVersion == 1`` and
``kMetadataCaptureTranslationContractVersion == 1`` and
``kMetadataGeometryTranslationContractVersion == 1`` and
``kMetadataDescriptiveTranslationContractVersion == 1``.

Workflow
--------

Translation is a separate step. Creation, editing, transfer, and writing do not
invoke it implicitly:

1. Read, create, or edit a finalized ``MetaStore``.
2. Call ``translate_xmp_creation_dates(...)``,
   ``translate_xmp_technical_metadata(...)``,
   ``translate_xmp_capture_metadata(...)``,
   ``translate_xmp_image_geometry(...)``,
   ``translate_xmp_descriptive_metadata(...)``, or the required combination
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

Conflict and removal policy
---------------------------

The date, technical, capture, geometry, and descriptive conflict-policy enums
apply the same three behaviors to each complete native group:

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

Invalid or lossy requests raise ``ValueError`` containing the C++ status,
mapping, and source entry ID. The original ``Document`` is not mutated.

Scope
-----

This milestone is intentionally limited to exact creation-date, common
technical and capture EXIF, target-bound orientation/stored dimensions, and
common descriptive mappings. It does not yet provide arbitrary EXIF/IPTC/XMP
translation, broader target layout/storage projection, multilingual-
alternative selection, timezone inference, numeric approximation or value
repair, or automatic synchronization during transfer.
