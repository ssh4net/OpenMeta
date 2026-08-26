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
- mutable fixed-width time patches;
- direct access to prepared bundles, blocks, routes, or execution plans;
- prepared payload/package persistence;
- editing or rewriting destination container bytes;
- encoder-specific object ownership or scheduling.

Those APIs remain available where documented, but their current contracts are
experimental. Keeping them outside Handoff v1 allows the internal writer and
package models to evolve without breaking high-performance codec integrations.
