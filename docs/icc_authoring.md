# ICC creation, editing and canonical serialization

OpenMeta 0.8.25 adds ICC serialization contract 1 in `openmeta/icc_serialize.h`.
Generic authoring and exact-key typed editing contract 2 accept ICC header
fields and raw tag payloads. These are C++ APIs; the logical-field and Python
creation/editing APIs retain their existing field sets.

## Shared lifecycle

Use `make_icc_header_field_key_view(offset)` or
`make_icc_tag_key_view(signature)` in `MetadataAuthoringEntry`, then call
`create_metadata_store()`. Keys and values are copied before a finalized output
is published. `edit_metadata_typed()` applies ordered Add/Set/Remove operations
with the existing occurrence, conflict, provenance, tombstone and resource rules.
Base/output aliasing is supported. Failed transactions leave the output intact.

Entry checks apply even when generic schema validation is disabled. They check
ICC key, value shape, version/date/signature rules and typed tag envelopes.
Whole-profile completeness and unique header/tag keys are checked during
serialization. Thus a valid entry transaction can produce an incomplete store
that cannot be serialized. Generic duplicate policy is unchanged: use
`RejectExactKeys` for construction-time uniqueness, or repair duplicates through
explicit occurrences before serializing.

## Header and tag inputs

Required explicit header offsets are 8 (version), 12 (device class),
16 (data color space), 20 (PCS), 24 (creation date) and 68 (illuminant).
The remaining supported offsets are 0, 4, 36, 40, 44, 48, 52, 56, 64, 80 and 84.
Fields never overlap. Header bytes 100–127 are reserved and emitted as zero.

| Header field | Typed representation | Raw alternative |
|---|---|---|
| 32-bit fields | Scalar U32 | Bytes of exactly 4 bytes |
| Attributes at 56 | Scalar U64 | Bytes of exactly 8 bytes |
| Creation date at 24 | U16 array of six calendar/time components | Bytes of exactly 12 bytes, big-endian |
| Illuminant at 68 | SRational array of three exactly representable signed 15.16 values | Bytes of exactly 12 bytes, big-endian |
| Profile ID at 84 | Bytes of exactly 16 bytes | Same |

Numeric encoding is big-endian. The illuminant uses integer conversion with
exact divisibility and signed 32-bit range checks; it is never encoded as three
TIFF numerator/denominator pairs. Version major must be 2 or 4, device class and
color-space signatures must be nonzero, and dates must be valid calendar dates.
Profile size at offset 0 and the `acsp` signature at 36 are derived; an explicit
signature must agree. Wire hints do not change canonical ICC encoding.

Raw values are `Bytes/U8/Unknown`. Numeric scalar/array hints are Unknown.
Tag signatures must be nonzero. Each tag is a raw byte payload of at least eight
bytes: a nonzero four-byte type signature, four zero reserved bytes, then the
host-provided data. Payload data remains opaque; OpenMeta does not synthesize
curves, matrices, calibration or transforms. Hosts supply the class-specific
required tags, coherent color information and any registered private payloads.
Successful serialization establishes this bounded binary contract; full profile
class/tag conformance and rendered color accuracy require host qualification.

## Measure and write

```cpp
#include "openmeta/icc_serialize.h"

const auto size = openmeta::serialize_icc_profile(store, {});
if (size.status != openmeta::IccProfileSerializeStatus::OutputTruncated)
    return;
std::vector<std::byte> profile(static_cast<size_t>(size.needed));
const auto written = openmeta::serialize_icc_profile(store, profile);
if (!written.ok())
    return;
```

The store must be finalized and immutable for the call. Active ICC entries are
selected; deleted entries and other families are ignored. Truncated/unreadable
entries, invalid spans or shapes, duplicate header offsets/tag signatures,
missing required headers and resource exhaustion fail before publication.
No active tags returns `NoIccData`; an empty profile is not emitted.

Tags sort by numeric signature, independent of source order. Tag data starts on
four-byte boundaries, with zero padding after every tag including the last.
Size, tag count and offsets are derived from checked layout arithmetic. Repeated
calls with the same store/options produce identical bytes; no clock or process
state enters serialization.

Default limits are 65,536 tags, 32 MiB per tag and 64 MiB output. Options must be
nonzero; complete output must fit uint32_t and the platform address space.
Input views and output must remain valid; output may not overlap the store arena.
An empty output measures without allocating a complete profile. A short output
receives the canonical prefix only after complete preflight and returns exact
`needed` plus `OutputTruncated`. Other failures report zero `written`/`needed`
and leave output bytes unchanged. Larger output tails stay unchanged.
Preparation allocates bounded scratch; allocation failure is not a recoverable
status. This is preparation, separate from allocation-free prepared patch replay.

`tags_serialized` counts selected tags. `header_fields_serialized` counts
accepted explicit header inputs, including a supplied derived field or an ID
cleared by policy. It does not count implicit zero/default header fields.
`failed_entry` identifies an attributable bad/oversized store entry.

## Profile ID and transfer

The default `IccProfileIdPolicy::Clear` emits zero ID bytes. `Preserve` carries an
explicit 16-byte ID supplied by the host. It neither computes nor verifies the
ICC profile hash; the host must ensure any preserved ID matches its final bytes.

Dirty ICC authoring/editing uses the strict canonical engine during transfer and
clears the ID. Dirty intent is detected before filtering, including a removed
entry's tombstone. Clean decoded transfers retain existing first-wins/skip,
ID-preservation and final-length compatibility rules. Both paths share checked
layout/emission and the corrected field-aware illuminant conversion.

Existing JPEG, TIFF/DNG, PNG, WebP and other ICC-capable targets retain their
carrier contracts. JXL ICC remains an encoder handoff. Removing one tag while
others remain can produce a rewritten profile. Removing the last tag does not
request target-carrier deletion: canonical serialization returns `NoIccData`,
and dirty ICC preparation fails rather than claiming a successful removal.

Version 0.8.25 retains ABI 4 and existing public layouts. Rebuild with matching
headers and binaries for the new symbols/contracts. Source snapshot 1, patch
contract 2, IIM serializer 1 and signer/adoption package wires are unchanged.
Hosts synchronize shared mutation; no atomics, mutexes or global IDs are added.

Binary layout reference: [ICC.1:2022, clauses 7.1–7.4](https://archive.color.org/specification/ICC.1-2022-05.pdf).
