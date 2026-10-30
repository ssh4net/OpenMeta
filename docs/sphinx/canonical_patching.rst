Prepared metadata patching
==========================

OpenMeta 0.8.24 provides prepared patch contract 2 in
``openmeta/metadata_patch.h``. One ``PreparedMetadataPatchPlan`` produces
unwrapped TIFF/EXIF, portable XMP, native IPTC-IIM, or any combination. It
compiles requests into opaque handles and creates independently owned
``PreparedMetadataPatchInstance`` workers. Workers survive plan destruction.

EXIF and IPTC requests select an exact key, active occurrence and native value
shape. Occurrence follows ``MetaStore::find_all()`` identity order, independently
of canonical wire sorting. Deleted entries are excluded. XMP requests select a
namespace URI and simple property path in the final portable serializer output,
with an exact escaped width. Prefix spelling is not the identity. Arrays,
qualifiers and nested structures cannot be selected for XMP patching; unrelated
packet content is retained. Native IPTC arrays are a separate raw-byte shape.

Preparation and worker creation may allocate. Patching, payload access and
library replay do not allocate. A mixed batch validates all handles, values,
widths and aliases before changing any payload. Payload addresses and sizes
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
The transaction guarantees all-or-nothing payload changes within a call,
without providing inter-thread synchronization.

XMP updates accept logical UTF-8/ASCII text. The library validates UTF-8 and XML
characters and escapes reserved characters. CR uses a character reference.
Escaped width must match exactly, with no padding, truncation, raw XML or lexical
normalization. EXIF retains typed little-endian encoding, fixed logical shapes
and nonzero rational denominators.

Native IPTC-IIM
---------------

Use ``make_iptc_dataset_key_view(record, dataset)`` and an exact expected native
shape, with ``escaped_width=0``. Bytes, Text and well-formed Arrays are supported,
including zero-width values. Scalar and Empty are not IIM payload shapes. The
canonical serializer captures source entry IDs and raw value spans while writing
its headers. Charset-first order, duplicate identity and short/extended length
headers remain unchanged. A patch copies only the selected value bytes; adding
datasets or changing widths requires a new preparation.

XMP-to-IPTC translation stores native IIM values as ``Bytes``. Derive the
expected shape from the selected entry and use ``make_value_view_bytes()``
for those updates; a logical text field does not imply Text storage. Authored
native Text is also supported.

IPTC Text preserves raw bytes and encoding hints without UTF/XML checks,
transcoding or NUL insertion. Arrays receive no numeric endian conversion or
rational-denominator validation. Field schemas and charset consistency are host
responsibilities; changing the charset marker does not transcode other fields.
The existing whole-store preparation validation is still enabled by default,
so it can reject a zero-denominator source array before serialization. This is
separate from raw IPTC update semantics.

``options.iptc`` defaults to 200,000 datasets, 8 MiB per dataset and 64 MiB total
IIM output. Limits must be nonzero and output must fit ``uint32_t`` and the
platform address space. Only requested families are serialized; unused-family
serializer options are ignored. ``MetadataPatchResult::iptc_status`` preserves
the underlying serializer status.

Replay and compatibility
------------------------

``payload(MetadataPatchPayload::ExifTiff)``, ``Xmp`` and ``IptcIim`` return
borrowed payload views. Replay preserves EXIF then XMP order and appends IPTC;
existing family enum values 0 and 1 are retained, with IPTC appended as 2.
A callback failure stops replay without undoing prior host output. Hosts own
container framing, checksums, encoded pixels and publication. No Python patch
binding is provided.

This release appends native IIM options/status to public structures within
unfrozen ABI 4. Rebuild with matching 0.8.24 headers and binaries. Source snapshot
v1, canonical IIM serializer contract 1, signer packages and Host Adoption
Profile v1 remain unchanged. The older 0.5.0 transition from EXIF-only patching
used ABI 3; its removed pre-0.5 names remain documented in the migration guide.

See ``docs/canonical_patching.md`` for the API example and
``docs/migration_0_5.md`` for the historical migration boundary.
