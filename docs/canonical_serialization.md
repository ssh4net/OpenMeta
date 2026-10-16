# Canonical Metadata Serialization

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

The current JPEG writer replaces a recognized Photoshop APP13 segment as a
whole. When IPTC shares that segment with other Photoshop resources, an IPTC
rewrite does not retain those resources. Canonical serialization does not change
that existing carrier policy. Resource-level APP13 merging remains an open writer
task; hosts that need it must preserve and wrap those resources themselves.

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
