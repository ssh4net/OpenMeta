Target-profile validation
=========================

OpenMeta 0.8.13 adds opt-in shared logical destination checks through
``MetadataValidationOptions::target_profile``. Creation, typed editing,
``validate_store()`` and canonical serialization share the checks. Target-profile
contract 1 is separate from detached validation contract 1. Default ``None``
keeps detached-store behavior.

The host must declare dimensions, three samples per pixel and EXIF wire policy
300 or 310. Optional expected ColorSpace (1/65535) and Interop index (R98/R03/THM)
are independent. Native primary Interop requires explicit expected-index authority;
no DCF or ICC relationship is inferred. These are primary-image EXIF 3.0 rule
subsets under the caller's version policy, not full EXIF 3.1 or file conformance.
DNG, thumbnails, GPS companions, codec markers, pixel data and storage byte bounds
remain outside this contract.

Profiles
--------

* ``ExifJpeg`` requires matching ExifIFD PixelX/YDimension, UNDEFINED
  ComponentsConfiguration ``{1,2,3,0}`` and IFD0 YCbCrPositioning 1/2. Marker-owned
  and forbidden primary TIFF storage fields must be absent.
* ``ExifTiffRgbChunky`` requires matching IFD0 dimensions, SHORT BitsPerSample
  ``{8,8,8}``, Compression 1, SamplesPerPixel 3 and PhotometricInterpretation 2.
  PlanarConfiguration is absent or 1. YCbCr fields are forbidden.
* ``ExifTiffRgbPlanar`` requires those RGB fields plus PlanarConfiguration 2.
* ``ExifTiffYCbCr`` uses PhotometricInterpretation 6, SHORT subsampling ``{2,1}``
  or ``{2,2}``, and positioning 1/2. PlanarConfiguration may be absent, 1 or 2.

All enabled profiles require primary ExifVersion and ColorSpace. Uncompressed
profiles exclude compressed-only PixelX/YDimension, ComponentsConfiguration,
CompressedBitsPerPixel and primary Interop. Explicit ``require_storage_tags``
adds positive RowsPerStrip, StripOffsets and StripByteCounts with exact paired
strip counts (per plane when planar). Storage bytes remain writer-owned.

Transaction and compatibility
-----------------------------

Only active exact primary keys count. Required types/counts/wire hints remain
checked with general schema validation disabled. Missing, forbidden, conflicting
or unauthorized fields produce structured ``TargetProfile*`` issues. The
validator never repairs metadata. ``validate_entry()`` does not impose store-wide
required companions. Construction/editing commit only a valid whole candidate;
serialization checks before writing bytes. Enabled profiles cannot be combined
with ``validate=false`` in construction or serialization. Limits remain bounded.

ABI label 4 stays unfrozen. The new option context enlarges public option layouts;
rebuild consumers with matching 0.8.13 headers and binaries. Source snapshot v1
and host synchronization are unchanged. See ``docs/target_profile_validation.md``
for the full field matrix, host example and limitations.
