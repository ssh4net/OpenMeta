# Prepared Canonical EXIF Patching

`openmeta/exif_tiff_patch.h` prepares immutable canonical TIFF/EXIF bytes and
compiles exact fixed-width fields into opaque handles. It is target-neutral:
the API does not select a container, emit route strings, or expose TIFF byte
offsets.

## Lifecycle

Preparation is the initialization phase and may allocate:

```cpp
#include "openmeta/exif_tiff_patch.h"

openmeta::ExifTiffPatchRequest request;
request.key = openmeta::make_exif_tag_key_view("exififd", 0x829aU);
request.expected.kind = openmeta::MetaValueKind::Scalar;
request.expected.elem_type = openmeta::MetaElementType::URational;
request.expected.count = 1U;

openmeta::ExifTiffPatchHandle handle;
openmeta::PreparedExifTiffPatchPlan plan;
const openmeta::ExifTiffPatchResult prepared =
    openmeta::prepare_exif_tiff_patch_plan(
        store,
        std::span<const openmeta::ExifTiffPatchRequest>(&request, 1U),
        {}, std::span<openmeta::ExifTiffPatchHandle>(&handle, 1U),
        &plan);
```

Each request identifies an exact `MetaKeyView`, zero-based duplicate
occurrence, and logical value shape. The output handle is scoped to the
prepared payload and request layout. It is not an offset and cannot be used to
modify a worker created from a different plan.
`kMaxPreparedExifTiffPatchHandles` is the hard per-plan handle ceiling; the
options default to a lower 4096-request resource bound.

Create one mutable worker instance per concurrent execution lane:

```cpp
openmeta::PreparedExifTiffPatchInstance worker;
openmeta::create_prepared_exif_tiff_patch_instance(plan, &worker);

const openmeta::ExifTiffPatchUpdate update {
    handle,
    openmeta::make_value_view_urational(1U, 250U),
};
const openmeta::ExifTiffPatchResult patched =
    openmeta::patch_prepared_exif_tiff_instance(
        &worker,
        std::span<const openmeta::ExifTiffPatchUpdate>(&update, 1U));

write_host_container(worker.payload());
```

Worker creation copies the prepared payload and may allocate. Patch batches and
`payload()` access do not allocate or resize storage. The immutable plan supports
concurrent const access. One mutable worker requires exclusive ownership while
patching; independent workers can be patched and replayed concurrently.

## Transaction Contract

A patch batch validates every update before changing bytes. Failure leaves the
complete worker payload unchanged. Validation rejects:

- invalid, duplicate, or foreign handles;
- logical kind, element type, encoding, or count changes;
- variable-width text, byte, or array replacements;
- zero rational denominators;
- values whose borrowed payload aliases the worker payload;
- stale or malformed slot bounds.

Scalar and array values use host-native `MetaValueView` input and are encoded to
canonical little-endian TIFF bytes. Text requests count content bytes; OpenMeta
preserves the serialized terminal NUL. ASCII and UTF-8 text are fixed-width byte
patches, not normalization or transcoding operations.

## Supported Fields

The v1 plan compiles source-backed EXIF/TIFF entries that the canonical classic
TIFF serializer emits with a fixed width. This includes 8/16/32-bit signed and
unsigned integers, float/double bit values, unsigned and signed rationals,
matching arrays, fixed byte payloads, and fixed-width ASCII/UTF-8 text.

Classic TIFF output does not serialize `U64` or `I64` integer entries, so those
requests return `EntryNotSerializable`. Regenerated IFD pointer tags and
synthetic fields such as an injected minimal DNG version are not patchable
because they do not identify a source store entry. Variable-count edits require
normal store editing followed by a new preparation pass.

Opaque MakerNotes remain governed by the serializer policy. Explicitly
preserving a raw MakerNote does not make vendor-private offsets, checksums, or
semantic fields safely editable.

## Container Boundary

`plan.payload()` and `worker.payload()` are unwrapped canonical TIFF bytes. A
host can replay them directly into PNG/WebP-style EXIF carriers or add its own
JPEG, JP2/JXL/BMFF, JPH, or private-container framing. Patching never requires a
`TransferTargetFormat`; host framing and image-data ordering remain outside this
contract.
