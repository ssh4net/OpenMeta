Prepared metadata patching
==========================

OpenMeta 0.5.0 replaces the EXIF-only patch API with
``openmeta/metadata_patch.h``. One ``PreparedMetadataPatchPlan`` produces
unwrapped TIFF/EXIF, portable XMP, or both. It compiles requests into opaque
handles and creates independently owned ``PreparedMetadataPatchInstance``
workers. Workers survive plan destruction.

EXIF requests select an exact key occurrence and native value shape. XMP
requests select a namespace URI and simple property path in the final portable
serializer output, with an exact escaped width. Existing standard/custom scalar
properties and scalar native projections are supported. Prefix spelling is not
the identity. Arrays, qualifiers and nested structures cannot be selected for
XMP patching; unrelated packet content is retained.

Preparation and worker creation may allocate. Patching, payload access and
library replay do not allocate. A mixed batch validates all handles, values,
widths and aliases before changing either payload. Payload addresses and sizes
remain stable on success and failure. The host synchronizes conflicting object
access, including borrowed payload reads and destruction. Input values remain
immutable during a call. The patch API uses no atomics, mutexes or global mutable
state and does not detect concurrent misuse.

Preparation requires a host-issued ``options.plan_id`` from 1 through
``kMaxMetadataPatchPlanId`` (48 bits). Default ID zero and out-of-range IDs return
``InvalidOptions``. Every successful preparation requires a fresh ID; do not
reuse it while any prior plan, worker or handle with that ID remains usable.
The host coordinates IDs across preparation callers. The library rejects handles
with different IDs but cannot detect host reuse of an ID for separate plans.
The batch transaction guarantees all-or-nothing payload changes within a call,
without providing inter-thread synchronization.

XMP updates accept logical UTF-8/ASCII text. The library validates UTF-8 and XML
characters and escapes reserved characters. CR uses a character reference.
Escaped width must match exactly, with no padding, truncation, raw XML or lexical
normalization. EXIF retains typed little-endian encoding, fixed logical shapes
and nonzero rational denominators.

``payload(MetadataPatchPayload::ExifTiff)`` and
``payload(MetadataPatchPayload::Xmp)`` return borrowed payload views.
``replay_prepared_metadata_instance`` visits EXIF then XMP. A callback failure
stops replay but cannot undo prior host output effects. Hosts own container
framing, checksums, encoded pixels and publication.

The independent patch contract is ``kMetadataPatchContractVersion``. The
0.5.0 release uses C++ ABI 3 and requires a rebuild. Pre-0.5 EXIF patch names and
headers are removed without compatibility aliases. The existing reader profile
and target-specific prepared-transfer handoff remain separate contracts.

See ``docs/canonical_patching.md`` for the full API example and
``docs/migration_0_5.md`` for the migration table and package version boundary.
