# Prepared Transfer Handoff

Prepared Transfer Handoff v1 is the stable target-preparation boundary for
applications that own an encoder, writer, SDK object, or realtime transcoding
pipeline. It prepares target-specific metadata once and exposes immutable,
typed operations without exposing `PreparedTransferBundle`, route strings, or
other transfer internals.

The contract is declared in
`openmeta/prepared_transfer_handoff.h`.

## Runtime Contract

```cpp
#include "openmeta/prepared_transfer_handoff.h"

if (openmeta::prepared_transfer_handoff_contract_version()
    != openmeta::kPreparedTransferHandoffContractVersion) {
    // Reject an incompatible linked OpenMeta library.
}
if (openmeta::prepared_transfer_handoff_instance_contract_version()
    != openmeta::kPreparedTransferHandoffInstanceContractVersion) {
    // Disable the mutable per-worker path.
}
```

`PreparedTransferHandoff` is move-only and has a fixed opaque-pointer public
layout. The linked OpenMeta library owns and destroys all prepared state.

## Prepare Once

```cpp
openmeta::PrepareTransferRequest request;
request.target_format = openmeta::TransferTargetFormat::Webp;
request.profile.safety = openmeta::TransferSafetyMode::RenderedImage;

openmeta::PreparedTransferHandoff handoff;
const openmeta::PreparedTransferHandoffResult prepared
    = openmeta::prepare_transfer_handoff(
        snapshot, request, openmeta::EmitTransferOptions {}, &handoff);
if (!prepared.ok()) {
    const std::string_view reason
        = openmeta::prepared_transfer_handoff_code_message(prepared.code);
    // Apply host policy or report `reason`, `prepare_code`, and `adapter_code`.
}
```

Preparation copies the decoded snapshot state needed by target serialization,
applies the requested transfer policy, packages payloads, compiles the typed
operation vector, and classifies each operation by metadata family. All
allocation and route interpretation happen in this phase. A failed call does
not replace an existing valid handoff.

Stable v1 requires
`TransferRawCarrierPassthroughMode::Disabled`. Opt-in raw-carrier reuse remains
an experimental policy and is rejected rather than silently changing stable
handoff behavior.

## Consume Typed Operations

Use `prepared_transfer_handoff_operation(...)` for indexed access. The returned
view contains:

- `semantic_kind`: EXIF, XMP, ICC, IPTC, JUMBF, C2PA, or unknown;
- `operation`: the stable enum-based marker, tag, box, chunk, item, property,
  or EXR operation and its target-specific fields;
- `payload`: the borrowed prepared carrier payload;
- `exr_name`, `exr_type_name`, `exr_value`, and `exr_is_opaque` for EXR
  attributes.

Hosts must treat `operation.block_index` as an opaque correlation value. It is
not an index into host-visible storage. For `ExrAttribute`, pass the explicit
EXR fields to the writer; the generic payload remains available only for
diagnostics.

Views borrow immutable handoff storage. They remain valid until the handoff is
successfully prepared again, reset, moved from, or destroyed.

## Replay Without Recompilation

`replay_prepared_transfer_handoff(...)` calls a `noexcept` function pointer once
per operation. A successful replay performs no allocation, route parsing,
operation-vector construction, validation rebuild, locking, or hidden retry.
The callback returns `TransferStatus::Ok` to continue; any other status stops
replay and reports the failed operation index.

The same immutable handoff can be replayed repeatedly. Concurrent const access
is supported when every replay uses independent callback state and the host
does not reset, move, destroy, or prepare the handoff concurrently.

## Per-Worker Mutable Instances

Use `PreparedTransferHandoffInstance` when frame or output metadata has
fixed-width time fields that change for each encoded image. Prepare one
immutable template, create one independently owned instance per worker, then
patch and replay that worker's instance:

```cpp
openmeta::PreparedTransferHandoffInstance worker;
openmeta::PreparedTransferHandoffResult created =
    openmeta::create_prepared_transfer_handoff_instance(handoff, &worker);

openmeta::PreparedTransferHandoffTimePatchFieldView field;
const openmeta::PreparedTransferHandoffPatchResult described =
    openmeta::prepared_transfer_handoff_instance_time_patch_field(
        worker, openmeta::TimePatchField::DateTime, &field);

constexpr char encoded_time[] = "2030:12:31 23:59:59";
static_assert(sizeof(encoded_time) == 20U); // EXIF ASCII including NUL
const openmeta::TimePatchView patch {
    openmeta::TimePatchField::DateTime,
    std::as_bytes(std::span<const char>(encoded_time, sizeof(encoded_time)))
};
const std::array<openmeta::TimePatchView, 1> patches = { patch };

if (created.ok() && described.ok() && field.width == sizeof(encoded_time)
    && openmeta::patch_prepared_transfer_handoff_instance(&worker, patches)
           .ok()) {
    openmeta::replay_prepared_transfer_handoff_instance(
        worker, emit_to_codec, codec);
}
```

Instance creation may allocate. It copies one contiguous payload buffer and
the compact operation, semantic, EXR-view, block-range, and patch-slot state.
It does not copy route strings, transfer-policy diagnostics, or generated
sidecars, and it does not recompile operations. The instance owns all copied
state and remains valid if the template is reset, moved, prepared again, or
destroyed.

`prepared_transfer_handoff_instance_time_patch_field(...)` reports the exact
serialized width and number of matching slots without exposing payload offsets.
This lets generic hosts size patch buffers for `SubSec*` and other fields whose
width is source-dependent. An absent field or non-uniform/invalid slot layout
is reported before patching.

Patching and replay allocate nothing. Stable instance v1 requires every patch
field to be valid, unique, and present in the prepared slot map. Values are
serialized bytes and must exactly match every corresponding slot width; for
EXIF ASCII date/time fields that normally includes the terminating NUL. Inputs
that alias mutable instance payload storage are rejected. The complete batch
is validated before any byte changes, so every failure leaves the instance
unchanged.

Each worker must own a separate instance. Independent instances may patch and
replay concurrently. A single instance requires exclusive ownership while it
is patched; indexed views or replay must not run concurrently with patch,
reset, move, or destruction. A borrowed operation view keeps its address but
its payload contents may change after a successful patch.

## Target Coverage

Handoff v1 exposes typed operations for:

| Target | Operation |
| --- | --- |
| JPEG | marker code and payload |
| TIFF, DNG | TIFF tag and payload |
| JPEG XL | box or ICC-profile operation |
| WebP | RIFF chunk type and payload |
| PNG | chunk type and payload |
| JP2 | box type and payload |
| HEIF, AVIF, CR3 | BMFF item/property fields and payload |
| EXR | attribute name, type, value, and opacity |

Format capability and metadata-family support remain separate questions. Use
`metadata_capability(...)` before assuming that a requested family can be
serialized into a target.

## Deliberate Boundary

Stable Handoff v1 does not include:

- source reading or snapshot persistence;
- raw-carrier passthrough;
- direct access to prepared bundles, blocks, routes, or execution plans;
- variable-width or structural metadata mutation;
- prepared payload/package persistence;
- editing or rewriting destination container bytes;
- encoder-specific object ownership or scheduling.

Those APIs remain available where documented, but their current contracts are
experimental. Keeping them outside Handoff v1 allows the internal writer and
package models to evolve without breaking high-performance codec integrations.
