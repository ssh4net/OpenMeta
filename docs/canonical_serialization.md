# Canonical Metadata Serialization

## Edited-snapshot C2PA signing (0.8.23)

`prepare_c2pa_signing_context()` combines an edited `TransferSourceSnapshot`
with the exact target bytes for JPEG, boxed JPEG XL, HEIF, AVIF and CR3. The
owned `PreparedTransferC2paSigningContext` retains the prepared metadata,
external-signer handoff and complete rewritten asset stream without C2PA.
Metadata preparation and content-binding results remain separate. In particular,
C2PA-only input can return `Unsupported/RequestedMetadataNotSerializable` for
metadata preparation and `Ok` for binding; check both results.

Pass the handoff to the host's manifest builder and signer, then use
`execute_prepared_c2pa_signing_context()` with the signer material and target
bytes. Validation replans the target and compares the complete binding stream
before staging or publishing output. A changed scan/codestream byte or prepared
metadata value fails. Changes confined to bytes removed or replaced by the
rewrite can pass when the complete binding stream and request semantics match.
The executor uses that fresh plan. JPEG signing contexts require complete marker
framing, bounded segment/SOS headers and an EOI marker; trailing bytes are
retained. This structural check does not decode or validate image pixels.
Contexts and source snapshots remain unchanged.
The host provides signature creation, certificate trust and cryptographic
verification; OpenMeta checks the existing structural signer contract.

`C2paSigningContextLimits` defaults to 200,000 snapshot entries and 64 MiB of
snapshot arena data. These two limits must be nonzero. `max_binding_bytes=0`
allows any binding size; set a nonzero limit to bound its materialization.
`OpenMetaResourcePolicy::max_file_bytes` independently caps target input bytes.
The decoded-store ceilings exclude separately owned raw carriers; bound
optional raw-carrier ingestion with the snapshot read/serialization limits.
These preparation, validation and execution operations allocate. Hosts must
synchronize access to shared mutable objects. Preflight failures publish no
context or output bytes; a byte writer's I/O failure retains the existing
partial-write behavior.

Python exposes `prepare_c2pa_signing_context(snapshot, target_bytes, ...)`,
returning independent `prepare_status`, `binding` and `overall_status` fields.
Set `jumbf_policy=TransferPolicyAction.Drop` to omit projected JUMBF
when requesting C2PA-only output. The returned `C2paSigningContext` provides `request()`, `validate(target_bytes)`
and `execute(target_bytes, ..., apply=True)`. Pass immutable Python `bytes` for
the target. Raw handoff/binding bytes require `unsafe_handoff_bytes()` and
`unsafe_binding_bytes()`; output asset bytes require `unsafe_execute()`.
These accessors export the retained stream and handoff issued at preparation;
they do not inspect another target. If the target changes before signing, call
`validate(current_target_bytes)` before exporting. Execution always validates
the supplied target again. The safe executor reports results without exposing
asset bytes. Python accepts
`policy`, `max_snapshot_entries`, `max_snapshot_arena_bytes` and
`max_binding_bytes` with the C++ defaults.

ABI remains 4; rebuild with matching 0.8.23 headers and binaries. Existing public
layouts, source snapshot v1 and external-signer package wire formats are unchanged.
Older preparation entrypoints remain available; use this explicit context API
when the source is an edited snapshot and signing requires target bytes.

## IPTC carrier removal (0.8.20)

Prepared bundles now carry explicit `remove_existing_iptc` edit intent. Preparation
sets it for JPEG/TIFF/DNG only when IPTC output is enabled, no active native IPTC
dataset remains, and at least one native dataset is marked dirty and deleted.
Omission, clean tombstones and disabled IPTC output do not request removal.
All datasets count, including charset/version records; delete those explicitly
when clearing the complete carrier. No automatic pruning is performed.

JPEG edits remove managed IPTC resources from leading Photoshop APP13 streams,
preserving unrelated resources byte for byte. TIFF/BigTIFF/DNG edits remove IFD0
tag 33723 and preserve other tags and pixel data. This does not erase every
possible embedded copy. Removal conflicts with a prepared IPTC payload. Fresh
emission and payload-only handoffs reject the edit-only intent; exact-output
packages and complete C2PA binding requests carry the edited bytes. The explicit
0.8.23 signing-context API supports edited snapshots plus target bytes; the
older snapshot preparation entrypoint alone cannot establish that binding.
Snapshot wire v1 is unchanged.

The bundle layout changes within unfrozen ABI 4. Rebuild consumers with matching
0.8.20 headers and binaries. Preparation may allocate; hosts synchronize shared
objects. General transfer and existing handoff wire versions remain unchanged.

## IPTC-IIM (0.8.15)

`openmeta/iptc_iim_serialize.h` adds `serialize_iptc_iim()` for native IPTC
datasets created, edited, translated or decoded into a finalized `MetaStore`.
It emits an unwrapped IIM stream. JPEG APP13/Photoshop framing and TIFF tag 33723
remain separate; transfer dataset rebuilds use the same wire engine.

```cpp
#include "openmeta/iptc_iim_serialize.h"
#include <vector>

const auto measured = openmeta::serialize_iptc_iim(store, {});
if (measured.status == openmeta::IptcIimSerializeStatus::OutputTruncated) {
    std::vector<std::byte> bytes(static_cast<size_t>(measured.needed));
    const auto written = openmeta::serialize_iptc_iim(store, bytes);
    // Check written.ok() before handing bytes to the container writer.
}
```

The v1 contract selects active IPTC datasets only. Other families and tombstones
are ignored. It preserves duplicate keys and values, putting CodedCharacterSet
(1:90) first, then sorting by `origin.order_in_block`, record, dataset, and original
entry order for ties. Lengths through 32,767 use the two-byte form; larger values
use `0x8004` followed by a four-byte big-endian length.

Bytes, Text and well-formed Array values supply **raw payload bytes**. Arrays
receive no numeric encoding or byte-order conversion. Text hints do not transcode
bytes or insert a charset marker. Unknown binary datasets and empty payloads are
retained. Use `translate_xmp_iptc_metadata()` for its supported text mappings and
UTF-8 marker policy. This serializer checks wire structure, not IIM field-level
semantics, standard field lengths, text validity or charset consistency.

The strict API rejects out-of-range keys, scalar/empty values, malformed spans or
counts, and selected Truncated/Unreadable entries. `failed_entry` identifies a
selected entry when applicable. Default ceilings are 200,000 datasets, 8 MiB per
dataset and 64 MiB output. Limits must be nonzero; output is capped at `UINT32_MAX`
and the platform's addressable size. Complete preflight precedes payload allocation
and publication. Measurement allocates no payload or ordering scratch. Writing may
allocate bounded scratch and a complete temporary payload; system allocation
failure is not a recoverable status.

A short output span receives a deterministic prefix and returns `OutputTruncated`.
All other errors leave output unchanged with zero `written` and `needed`.
Output must not overlap the store's arena; detected overlap returns
`InvalidOptions`. Callers keep the store immutable during the call and synchronize
mutations. This is a preparation API, not allocation-free prepared patching.

JPEG/TIFF transfer retains its best-effort policy: unsupported or damaged datasets
are skipped with a warning; resource-limit failure rejects the complete rebuild.
An unchanged opaque Photoshop IPTC carrier still takes precedence when no native
IPTC dataset is dirty. That carrier pass-through is outside canonical dataset
validation. Existing public layouts and development ABI 4 remain unchanged;
consumers using the new symbols need matching 0.8.15 headers and binaries.

### JPEG IPTC replacement (0.8.16)

When a nonempty `jpeg:app13-iptc` replacement is requested, JPEG edit planning,
buffered apply and package construction validate complete Photoshop `8BIM`
resource streams. The replacement must contain at least one `0x0404` IPTC
resource and no other resource IDs. Malformed streams return `Malformed`;
mixed or IPTC-free replacement streams return `Unsupported`.

All old `0x0404` records in leading Photoshop APP13 segments are removed.
Every other record retains its exact signature, ID, name, data and padding,
including duplicate IDs and nonzero padding. Each mixed source segment yields
one residual APP13 at its original position; replacement segments are inserted
once at the first replaced marker. Resources from different sources and the
replacement are never merged, keeping each payload within 65,533 bytes.
Photoshop segments without IPTC remain byte-identical. Without an IPTC update,
APP13 resource parsing is bypassed and existing opaque bytes remain unchanged.
Post-SOS data stays opaque. This does not update semantic dependencies such as
Photoshop IPTC digest resources; preserved records are not recertified.

Mixed resources or extra old IPTC segments require `MetadataRewrite`.
`InPlace` remains available for matching IPTC-only segment counts and payload
sizes. Plan sizes and counters are rechecked before output allocation. Plans
remain summaries, not source identity bindings: keep the input and bundle
immutable between planning and execution. Apply/package failures leave caller
outputs unchanged; writer preflight errors make no writes. Sink failures can
still leave partial external output. Host code synchronizes shared objects.

`emitted_segments` and rewrite `appended_segments` count replacement plus
residual output segments. `replaced_segments`, `removed_existing_segments`
and apply `skipped` count original marker segments replaced, even when their
non-IPTC resources survive in a residual segment.

C2PA rewrite binding includes each residual through
`PreservedJpegPhotoshopResources`: `source_offset` identifies its original APP13
marker, `size` includes the emitted four-byte marker header, `block_index` is
`UINT32_MAX`, and `jpeg_marker_code` is `0xED`. It counts as a prepared segment.
Handoff/signed packages use wire version 2 only when this kind is present;
legacy kinds retain version 1. Readers reject unknown kinds and the new kind
in version 1. Binding equality applies to the prepared rewrite options; changing
edit options such as XMP stripping after preparing a signing request is outside
that binding contract. This adds no cryptographic signer or prepared IPTC patch API.

## EXIF/TIFF

`openmeta/exif_tiff_serialize.h` exposes the same deterministic EXIF/TIFF
serializer used by target transfer preparation without requiring a fake target
container.

## Measure And Write

```cpp
#include "openmeta/exif_tiff_serialize.h"

#include <cstddef>
#include <vector>

const openmeta::ExifTiffSerializeResult measured =
    openmeta::serialize_exif_tiff(store, {});
if (measured.status !=
    openmeta::ExifTiffSerializeStatus::OutputTruncated) {
    // No EXIF data, invalid metadata, or a resource failure.
}

std::vector<std::byte> bytes(static_cast<size_t>(measured.needed));
const openmeta::ExifTiffSerializeResult written =
    openmeta::serialize_exif_tiff(store, bytes);
```

The output is an unwrapped little-endian TIFF byte stream beginning with `II`
and TIFF magic `42`. It contains neither an `Exif\0\0` preamble nor container
framing. `needed` supports exact capacity planning; a short span receives a
deterministic prefix and returns `OutputTruncated`.

Repeated calls against an immutable finalized store are deterministic and
safe to run concurrently. Serialization is a preparation operation and may
allocate internally. For a realtime or repeated-image path, serialize once,
retain the immutable bytes, and replay that caller-owned buffer without any
OpenMeta allocation. Per-frame fields should stay in a host-owned fixed header
unless they must also appear in EXIF/XMP.

## Carrier Wrapping

The canonical payload maps to current transfer carriers as follows:

| Destination | Prepared EXIF payload |
| --- | --- |
| JPEG and current TIFF/DNG backend handoff | `Exif\0\0` + TIFF |
| PNG `eXIf` and WebP `EXIF` | TIFF |
| JP2/JXL/BMFF EXIF carrier | big-endian offset `6` + `Exif\0\0` + TIFF |

`prepare_metadata_for_target()` now applies these wrappers to the canonical
payload. Hosts writing a different container should own its framing and use
the canonical bytes directly.

For fixed-width values that must change during repeated execution,
[`metadata_patch.h`](../src/include/openmeta/metadata_patch.h) compiles exact
key occurrences into opaque plan-scoped handles. Its immutable canonical plan,
per-worker instances, transactional typed patches, and payload replay remain
independent of container selection. See
[`canonical_patching.md`](canonical_patching.md). Do not retain or infer private
TIFF byte offsets.

## Policy And Limits

The serializer validates finalized stores by default, honors compatible TIFF
wire-type hints, and applies a caller-specified output limit. Opaque MakerNotes
are dropped by default. `PreserveOpaque` retains only an existing raw MakerNote
value; it does not reconstruct decoded vendor tables, relocate vendor-private
offsets, or repair checksums.

`include_subifds` and `inject_minimal_dng_version` are explicit because they are
RAW/DNG-oriented choices. A syntactically valid payload can still contain
image-dependent metadata that is wrong for the destination. Build and validate
against the actual encoded image specification before persistence.
