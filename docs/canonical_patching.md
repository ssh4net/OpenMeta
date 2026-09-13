# Prepared metadata patching

OpenMeta 0.5.0 provides `openmeta/metadata_patch.h` for unwrapped canonical
TIFF/EXIF and portable XMP payloads. One plan can contain either family or both.
One update batch validates every requested change before writing either payload.

This replaces the pre-0.5 `exif_tiff_patch.h` API. See
[migration_0_5.md](migration_0_5.md) for the source and binary migration.

## Preparation and worker ownership

Prepare after authoring a finalized store. For EXIF, select an exact key,
zero-based occurrence and native value shape. For XMP, select the namespace URI
and simple property path in the serializer output, plus the exact escaped width.

```cpp
#include "openmeta/metadata_patch.h"
#include <array>

std::array<openmeta::MetadataPatchRequest, 2> requests;
requests[0].key = openmeta::make_exif_tag_key_view("exififd", 0x9211U);
requests[0].expected.kind = openmeta::MetaValueKind::Scalar;
requests[0].expected.elem_type = openmeta::MetaElementType::U32;
requests[0].expected.count = 1U;
requests[1].key = openmeta::make_xmp_property_key_view(
    "https://example.test/capture/1.0/", "FrameNumber");
requests[1].escaped_width = 10U; // The store already emits ten text bytes.

std::array<openmeta::MetadataPatchHandle, 2> handles;
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

## Transactional EXIF and XMP updates

```cpp
const std::array<openmeta::MetadataPatchUpdate, 2> updates = {{
    { handles[0], openmeta::make_value_view_u32(42U) },
    { handles[1], openmeta::make_value_view_text(
          "0000000042", openmeta::TextEncoding::Utf8) },
}};
const openmeta::MetadataPatchResult patched =
    openmeta::patch_prepared_metadata_instance(&worker, updates);
if (!patched.ok()) {
    // Every EXIF and XMP payload byte still has its previous value.
    return;
}
const auto exif = worker.payload(openmeta::MetadataPatchPayload::ExifTiff);
const auto xmp = worker.payload(openmeta::MetadataPatchPayload::Xmp);
// Copy these compact payloads into the host's preallocated frame buffers.
```

Successful and rejected batches, payload access and library replay allocate no
heap memory. Payload addresses and lengths remain stable. Input values must
remain immutable throughout the call and cannot borrow bytes from either worker
payload. Duplicate, invalid, foreign and stale-generation handles fail before
any write when the host follows the ID lifetime contract. Transactional here
means all-or-nothing payload changes within one call; it does not provide
inter-thread synchronization. A valid early EXIF update followed by an invalid
XMP update changes neither family. A caller that keeps both families in one plan needs no separate
EXIF/XMP preflight or rollback layer.

`replay_prepared_metadata_instance` calls a synchronous callback in EXIF then
XMP order, skipping absent families. The callback owns any output effects and
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

## Container ownership

Payloads carry no JPEG markers, PNG chunks, JP2/JPH UUID boxes or image data.
The host owns framing, lengths, offsets, checksums, encoders and final I/O.
The existing prepared-transfer handoff remains useful for target-specific typed
operations and replay. Its time-field operations are not aliases for this new
standalone payload API. A new generic property request should use this API.
