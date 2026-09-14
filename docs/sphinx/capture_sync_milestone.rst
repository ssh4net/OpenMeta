Capture Synchronization Milestone
====================================

Audit date: 2026-09-14. This inventory describes C++ 0.5.4 and does not change
runtime behavior. Ten explicit reverse APIs cover **46 distinct ExifIFD tags**,
including camera text and excluding GPS, dates, geometry, IPTC and MakerNotes.
This count does not measure all-EXIF or competitor coverage.

Implemented Targets
-------------------

The API stems below have the prefix ``translate_xmp_`` and suffix
``_metadata``. Detailed mappings, aliases, limits and removal rules are in
:doc:`translation`.

.. list-table::
   :header-rows: 1
   :widths: 35 15 50

   * - API stem
     - Targets
     - Scope
   * - capture
     - 5
     - Exposure time, f-number, ISO, focal length, exposure bias
   * - capture_settings
     - 12
     - Closed capture-setting enums
   * - capture_rational
     - 4
     - Subject distance, digital zoom, exposure index, flash energy
   * - flash
     - 1
     - Flash bitfield
   * - light_source
     - 1
     - Defined light-source codes
   * - sensitivity
     - 7
     - ISO and its type/companion group
   * - camera_text
     - 6
     - Spectral, camera and lens printable-ASCII fields
   * - identity
     - 2
     - LensSpecification and ImageUniqueID
   * - apex
     - 5
     - Direct APEX values, including exposure bias
   * - capture_spatial
     - 5
     - Focal-plane resolution/unit and subject arrays

ISO and ExposureBiasValue are each shared by two APIs. The 48 mappings
therefore cover 46 distinct tags.

Combined Qualification and Gaps
------------------------------------

Ordinary and fractional fixtures pass exact native type/count/value checks for
all 46 targets in JPEG and classic TIFF with explicit ``ReplaceExisting``.
Reversing the call order gives the same native result; repeated calls add no
entries. These are bounded fixtures, not every accepted value or container.

The audit identifies these portable-output gaps in 0.5.4:

* FNumber ``17/6`` emits ``2.8``, which reverses to ``14/5``.
* FocalLength ``50/3`` emits ``16.7 mm``, which reverses to ``167/10``.
* DigitalZoomRatio ``1/3`` emits ``0.333333333333333``. The exact reverse
  parser rejects that decimal as unrepresentable, rolling back the complete
  four-field rational call. ExposureTime, SubjectDistance, ExposureIndex and
  FlashEnergy retain their fractions in the tested packets.
* Existing ``exif:ISO`` and generated ``exifEX:PhotographicSensitivity`` can
  both survive, including with ``CanonicalizeManaged``. The sensitivity
  reverse API correctly rejects the eligible duplicate aliases.
* A malformed native SHORT for FNumber, FocalLength or DigitalZoomRatio can
  claim the property without emitting a value under ``CurrentBehavior``,
  hiding valid existing XMP. ``ExistingWins`` retains it in these fixtures.

``ExistingWins`` with ``PreserveAll`` retains the source fractions but still
has the ISO alias collision. Emitting only retained source XMP, with EXIF
projection disabled, gives an exact combined reverse round trip in both
fixtures. That choice requires complete authoritative XMP and does not
publish subsequent native-only edits.

Composition and Next Batch
--------------------------

Each reverse call is transactional. A sequence of calls is not one library
transaction. Stage a candidate store/document and publish it only after every
call succeeds. A late invalid SubjectLocation leaves the original document
and the earlier successful Python candidate intact in the audit.

With ``All`` and default ``FailOnConflict``, basic capture can create ISO before
the sensitivity call, which then rejects the incomplete native group. Calling
sensitivity first passes in the fixture. Explicit replacement passes in either
order. Assign ownership of shared targets or select a deliberate conflict
policy. Retain host synchronization for conflicting shared-object access.

Transfer does not invoke reverse translation. Source XMP, native metadata and
destination carriers need explicit authority choices. Packet absence does not
encode a deletion request; tombstones and carrier cleanup follow separate
contracts. See :doc:`xmp_sync_policy`.

The next batch should retain API signatures and close these gaps together:

1. Exact FNumber, FocalLength and DigitalZoomRatio output, including boundaries
   and permitted zero values.
2. Native scalar type/count validation before claiming generated properties.
3. Managed sensitivity alias reconciliation only when valid generated
   replacements exist, with legacy/indexed paths and conflict cases covered.
4. Combined ten-API, policy, rollback and JPEG/classic TIFF/BigTIFF checks,
   independent reads, and one platform matrix against the final patch.

Further field batches remain separate: additional capture scalars; six
environment values; image encoding/calibration fields; composite capture
groups; structured/opaque OECF, CFA and device data; UserComment and EXIF text
encoding/version extensions. MakerNote rewrite trust remains vendor-specific.
Read/display support does not establish reverse writeback support. Downstream
application acceptance and whole-corpus qualification are outside this audit.
