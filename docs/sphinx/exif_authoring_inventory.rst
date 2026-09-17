Standard EXIF Authoring Inventory
=================================

Audit date: 2026-09-17. Baseline: **C++ 0.5.9**, ABI 3, commit ``6d04da1``.
This is an inventory and next-batch decision; library behavior is unchanged.

Scope and counts
----------------

Sources are `EXIF 3.0, corrected 2024 edition`_, `EXIF 3.1, 2026 edition`_
and the `2026 XMP mapping specification`_. Counts cover primary-image data
fields in Tables 6, 8, 9, 14 and 16. They exclude duplicate thumbnail instances,
TIFF/EP, DNG, vendor fields and structural pointers 8769, 8825 and A005.
Tag IDs are hexadecimal.

.. list-table::
   :header-rows: 1
   :widths: 35 15 20 30

   * - Group
     - Fields
     - Reverse targets
     - Boundary
   * - TIFF/IFD0, Table 6
     - 30
     - 7
     - Four technical and three geometry fields
   * - ExifIFD, EXIF 3.0
     - 88
     - 85
     - 75 capture, eight date, two geometry fields
   * - ExifIFD, EXIF 3.1
     - 95
     - 85
     - Seven new fields
   * - GPSIFD, Table 14
     - 32
     - 31
     - GPSVersionID is a managed companion
   * - InteropIFD, Table 16
     - 1
     - 0
     - InteroperabilityIndex; other registry entries are extensions

The ExifIFD editions overlap and must not be added. A reverse route is a bounded
XMP-to-native contract, not all-value support, complete-file conformance or
competitor parity. Generic exact-key authoring and registry names are separate.
The sixteen capture APIs have 80 mappings to 75 tags. ISO, ExposureBiasValue,
CameraOwnerName, LensMake and LensModel each have two owners; see
:doc:`capture_sync_milestone`.

Validation findings and selected next batch
-------------------------------------------

Fixed schema entries cover 17/30 IFD0 fields, 45/95 ExifIFD fields, 9/32 GPS
fields and 0/1 Interop fields. These counts do not establish semantic validation.
**64 existing reverse targets lack fixed schema entries:** 41 ExifIFD and
23 GPSIFD fields. All seven routed IFD0 fields have entries.

Runtime probes against 0.5.9 reproduce these boundaries:

* Text at MeteringMode 9207 is accepted under the default unknown-tag policy.
  A valid SHORT at that key is rejected as ``UnknownExifTag`` under strict policy.
* Valid ASCII ``R98`` at InteroperabilityIndex 0001 is rejected as ``WrongIfd``.
  The fallback recognizes the same numeric ID in GPSIFD, with no Interop schema.
* Detached authoring accepts ImageTitle without ExifVersion, Photographer
  without Artist and reserved ColorSpace 42. The EXIF text translator accepts
  CameraOwnerName without Artist. UTF-8 Make remains outside the documented
  ASCII-only technical reverse contract.

The next coherent batch is validation of existing writeback fields:

1. Add type/count/singleton rules for the 64 routed fields; reuse existing enum,
   bitfield, rational-sentinel, array and text validators. Check detailed tag
   definitions, not only summary-table wire types.
2. Recognize the standard Interop field and legal reused IDs. Cover registered
   Interop extensions as needed while preserving true misplaced-tag diagnostics.
3. Check a consistent native contract through authoring, typed editing,
   translation and serialization, including malformed values, wire hints,
   unknown-tag policies, duplicates, limits and rollback.
4. Qualify combined capture/GPS values, portable output, snapshots and container
   round trips together; run one final platform matrix for the implemented batch.

Preserve documented valid translator output and sentinels. Keep unknown/private
policy explicit and document tighter rejection of malformed known fields.
Detached stores must not acquire mandatory complete-file requirements or
inferred companions. Conflicting shared-object access remains host-synchronized.

Remaining writeback and profile work
------------------------------------

The three remaining EXIF 3.0 ExifIFD fields are ColorSpace A001 (host target
override exists; color/ICC/Interop agreement needs a contract), RelatedSoundFile
A004 (host file association) and MakerNote 927C (separate vendor rewrite trust).

After validation, the next writeback family is the six EXIF 3.1
development/correction fields A40D--A412:

* DevelopmentType: SHORT containing two bytes, each chosen from 1, 2 or 4;
  XMP uses a structure.
* DevelopmentTypeDescription: native UTF-8 type 129, even for ASCII characters.
* DistortionCorrection, ChromaticAberrationCorrection, ShadingCorrection:
  SHORT 0 or 1. NoiseReduction: SHORT 0 through 3.

DC-010-2026 Table A.6 spells the structure fields
``exifEX:DevelopmentCharacterstic`` and ``exifEX:FactoryDefault``. Preserve that
published spelling and use namespace URI ``http://cipa.jp/exif/1.0/``.
Define native/XMP rendered-image retention together; do not infer processing
history from RAW or MakerNote data.

Version policy needs an explicit decision: DC-008-2026 section 4.6.6.1.1 says
``0300`` in English (PDF page 58) and the `Japanese original`_ (PDF page 60),
whereas DC-010-2026 Table 7 describes EXIF 3.1 as ``0310`` (PDF page 14).
Record the source discrepancy and chosen host-supplied policy; do not silently
repair the text or automatically upgrade versions. LearningOptOutIn 9287 is
the seventh new field and needs a separate bounded usage/intention contract.

IFD0 ImageDescription, Artist and Copyright lack dedicated reverse routes;
descriptive reverse currently targets IPTC. Broader UTF-8, two-part Copyright,
Artist/owner/photographer and Software/editor relationships need profile work.
The other twenty unrouted Table 6 fields describe raster/color/layout facts and
need host/encoder authority. Full mandatory-tag and color/Interop validation
requires file/container/version context beyond today's dimensions/CFA context.

Downstream OIIO/iRAW acceptance stays on hold. C/Rust and vendor rewrite
guarantees are unchanged. Fuzzy search remains lowest priority.

.. _EXIF 3.0, corrected 2024 edition: https://www.cipa.jp/std/documents/download_e.html?CIPA_DC-008-2024-E=
.. _EXIF 3.1, 2026 edition: https://www.cipa.jp/std/documents/download_e.html?CIPA_DC-008-2026-E=
.. _2026 XMP mapping specification: https://www.cipa.jp/std/documents/download_e.html?CIPA_DC-010-2026=
.. _Japanese original: https://www.cipa.jp/std/documents/download_j.html?CIPA_DC-008-2026-J=
