# Canonical Metadata Serialization

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

The current time-patch map remains target-specific and is still exposed
through prepared transfer handoffs. General target-neutral compiled patch
handles are a later milestone; do not retain or infer private TIFF byte offsets.

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
