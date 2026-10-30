# Prepared metadata patching

OpenMeta 0.8.24 provides prepared patch contract 2 in
`openmeta/metadata_patch.h` for unwrapped canonical TIFF/EXIF, portable XMP and
native IPTC-IIM payloads. One plan can contain any combination of these families.
One update batch validates every requested change before writing any payload.

This replaces the pre-0.5 `exif_tiff_patch.h` API. See
[migration_0_5.md](migration_0_5.md) for the source and binary migration.

## Preparation and worker ownership

Prepare after authoring a finalized store. For EXIF and native IPTC, select an exact key,
zero-based active occurrence and native value shape. Occurrence follows
`MetaStore::find_all()` order, independently of the serializer's wire order;
deleted entries are excluded. For XMP, select the namespace URI
and simple property path in the serializer output, plus the exact escaped width.

```cpp
#include "openmeta/metadata_patch.h"
#include <array>

std::array<openmeta::MetadataPatchRequest, 3> requests;
requests[0].key = openmeta::make_exif_tag_key_view("exififd", 0x9211U);
requests[0].expected.kind = openmeta::MetaValueKind::Scalar;
requests[0].expected.elem_type = openmeta::MetaElementType::U32;
requests[0].expected.count = 1U;
requests[1].key = openmeta::make_xmp_property_key_view(
    "https://example.test/capture/1.0/", "FrameNumber");
requests[1].escaped_width = 10U; // The store already emits ten text bytes.
requests[2].key = openmeta::make_iptc_dataset_key_view(2U, 5U);
requests[2].expected = { openmeta::MetaValueKind::Text,
                         openmeta::MetaElementType::U8,
                         openmeta::TextEncoding::Utf8, 8U };

std::array<openmeta::MetadataPatchHandle, 3> handles;
openmeta::PreparedMetadataPatchPlan plan;
openmeta::MetadataPatchPlanOptions options;
options.plan_id = 1U; // Example host-issued ID; use a fresh ID for each preparation.
const openmeta::MetadataPatchResult prepared =
    openmeta::prepare_metadata_patch_plan(store, requests, options, handles, &plan);
if (!prepared.ok()) {
    // Report prepared.code and prepared.failed_index outside capture.
    return;
}
openmeta::PreparedMetadataPatchInstance worker;
if (!openmeta::create_prepared_metadata_patch_instance(plan, &worker).ok()) {
    return;
}
```

Preparation and worker creation may allocate. Each worker owns its payloads and
compiled slots and survives destruction of the plan. The host controls
synchronization, ownership and publication. Each mutable worker requires exclusive
access, including against reads through borrowed payload views and destruction. Neither the
source store nor a template is modified by patching.

The serializer records the XMP locations internally after applying its conflict
policy. Namespace prefixes and text sentinels are not application identifiers.
The default patch options include existing XMP, preserve custom namespaces and
let existing XMP win over generated projections. Scalar EXIF/IPTC projections
can also be selected by their emitted XMP identity. Only requested payload
families are generated.

## Host-owned preparation identity and synchronization

Set `options.plan_id` to a host-issued value from 1 through
`kMaxMetadataPatchPlanId` (48 bits). Zero and larger values return
`InvalidOptions`. Each successful preparation needs a fresh ID, even when the
serialized packet is identical. An ID may be reused only after all prior plans,
workers and handles with that ID can no longer be used. A host sequence must
coordinate all preparation callers; separate per-thread counters starting at the
same value do not provide distinct IDs. Exhaustion requires the host to stop or
establish that old IDs are no longer usable; do not silently wrap.

The library stores and compares the supplied identity. It cannot detect duplicate
IDs assigned to separate plans by the host. Preparation, worker creation,
patching and replay use no atomics, mutexes or global mutable state. The host
must synchronize conflicting access and object lifetime; concurrent misuse is
not detected and may cause undefined behavior. Independent plans and workers
with independent writable buffers do not share patch state.

## Transactional EXIF, XMP and IPTC updates

```cpp
const std::array<openmeta::MetadataPatchUpdate, 3> updates = {{
    { handles[0], openmeta::make_value_view_u32(42U) },
    { handles[1], openmeta::make_value_view_text(
          "0000000042", openmeta::TextEncoding::Utf8) },
    { handles[2], openmeta::make_value_view_text(
          "Shot0042", openmeta::TextEncoding::Utf8) },
}};
const openmeta::MetadataPatchResult patched =
    openmeta::patch_prepared_metadata_instance(&worker, updates);
if (!patched.ok()) {
    // Every EXIF, XMP and IPTC payload byte still has its previous value.
    return;
}
const auto exif = worker.payload(openmeta::MetadataPatchPayload::ExifTiff);
const auto xmp = worker.payload(openmeta::MetadataPatchPayload::Xmp);
const auto iptc = worker.payload(openmeta::MetadataPatchPayload::IptcIim);
// Copy these compact payloads into the host's preallocated frame buffers.
```

Successful and rejected batches, payload access and library replay allocate no
heap memory. Payload addresses and lengths remain stable. Input values must
remain immutable throughout the call and cannot borrow bytes from any worker
payload. Duplicate, invalid, foreign and stale-generation handles fail before
any write when the host follows the ID lifetime contract. Transactional here
means all-or-nothing payload changes within one call; it does not provide
inter-thread synchronization. A valid early EXIF or XMP update followed by an invalid
IPTC update changes no payload. The reverse is also true. A caller that keeps
all families in one plan needs no separate cross-family rollback layer.

`replay_prepared_metadata_instance` calls a synchronous callback in EXIF, XMP, then
IPTC order, skipping absent families. The callback owns any output effects and
must meet the application's allocation requirements. A false callback stops
replay; already performed host writes are not rolled back. Do not publish a
partially written frame, or mutate/reset the worker from a replay callback.

## Values and bounds

EXIF uses host-native `MetaValueView` values and canonical little-endian TIFF
encoding. The logical kind, element type, encoding and count must match the
compiled request. Fixed arrays and byte values are supported. Text counts omit
the serialized terminal NUL. Rational denominators must be nonzero. Regenerated
IFD pointers, synthetic entries and values the classic TIFF serializer omits
cannot be selected.

XMP accepts logical UTF-8 or ASCII text for existing simple scalar properties.
It validates UTF-8 and XML 1.0 characters, then escapes `&`, `<`, `>`, quotes and
apostrophes. CR is emitted as `&#xD;` to preserve it through XML line-ending
normalization. Escaped output must have exactly the prepared width: `&` consumes
five bytes as `&amp;`. There is no padding, truncation, raw-XML insertion or
numeric/date reformatting. Leading zeros and subsecond digits remain intact.
An empty scalar can only be replaced with another zero-width value.

The character rules follow [XML 1.0](https://www.w3.org/TR/xml/#charsets) and
[RFC 3629](https://www.rfc-editor.org/rfc/rfc3629#section-4).

Array items, language alternatives, qualifiers, nested structures, new properties,
new namespace declarations and variable-width changes are outside the initial
XMP patch contract. Such properties elsewhere in the packet remain unchanged.
Use store editing and serialization to prepare a new layout at a stopped or
drained worker boundary.

The default request limit is 4096; the hard handle ceiling is 65534. XMP output
has a configurable nonzero bound, defaulting to 16 MiB and 65536 emitted entries.
These are library defaults, not measured camera packet sizes or latency claims.
A complete oversized packet fails preparation rather than publishing a partial
patch plan.

## Native IPTC-IIM values

Select `make_iptc_dataset_key_view(record, dataset)` with `escaped_width=0`.
Both key components must fit in eight bits. The expected kind, element type,
count and text hint must match the selected source value. Bytes, Text and
well-formed Arrays are supported, including zero-width values; Scalar and Empty
are not IIM payload shapes. Zero-width selections remain valid slots and can
only receive another value of the same zero-width shape.

The canonical IIM serializer records source entry identity and the exact raw
value span while writing its headers. Charset-first ordering, source order,
duplicates and short/extended length fields are retained. Patching changes only
those raw value bytes; it never rewrites dataset headers, adds datasets, pads or
resizes the packet. Changing a field's width requires a new preparation.

XMP-to-IPTC translation currently represents native IIM datasets as `Bytes`.
Derive the expected shape from the selected entry and use
`make_value_view_bytes()` for those updates; a logical text field does not
imply `MetaValueKind::Text` storage. Authored native Text remains supported.

Native IPTC Text is raw data: encoding hints are retained without UTF/XML
validation, transcoding or NUL insertion. Arrays are raw bytes with a checked
shape, without endian conversion or rational-denominator checks. Field schemas,
standard text lengths and charset consistency remain host responsibilities.
Updating the charset marker does not convert any other dataset. Existing whole-
store preparation validation (`options.validate`, true by default) still applies;
its generic checks can reject a source rational array with zero denominators.
That preparation policy is separate from raw IPTC updates.

`options.iptc` uses the canonical IIM limits: 200,000 datasets, 8 MiB per dataset
and 64 MiB output by default. Limits must be nonzero, and output must fit
`uint32_t` and the platform address space. Only requested families are generated;
unused-family serializer options are ignored. `MetadataPatchResult::iptc_status`
reports the underlying serializer status. Existing payload enum values remain
EXIF=0 and XMP=1; native IPTC is appended as 2, including in replay order.

This release appends native IIM options/status to public structures within
unfrozen ABI 4. Rebuild consumers with matching 0.8.24 headers and binaries.
Source snapshot v1, canonical IIM serializer contract 1, signer packages and
Host Adoption Profile v1 are unchanged. Prepared patching is a C++ API; it has
no Python binding. No atomics, mutexes or automatic cross-family synchronization
were added.

## Container ownership

Payloads carry no JPEG markers, PNG chunks, JP2/JPH UUID boxes or image data.
The host owns framing, lengths, offsets, checksums, encoders and final I/O.
The existing prepared-transfer handoff remains useful for target-specific typed
operations and replay. Its time-field operations are not aliases for this new
standalone payload API. A new generic property request should use this API.
