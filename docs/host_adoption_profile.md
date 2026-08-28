# Host Adoption Profile

Host Adoption Profile v1 is the narrow compatibility boundary for applications
that integrate OpenMeta into high-throughput image processing or transcoding.
It is host-neutral: the source may be memory, a file handle, a range-backed
object, or another synchronous positional I/O service.

The profile separates stable state and I/O contracts from broader transfer
implementation details that are still evolving.

## Runtime Contract Check

Include `openmeta/host_adoption.h` and compare the profile compiled into the
application with the linked OpenMeta library:

```cpp
#include "openmeta/host_adoption.h"

if (!openmeta::host_adoption_profile_matches(
        openmeta::kHostAdoptionProfileV1)) {
    // Reject the incompatible OpenMeta runtime before processing assets.
}
```

`HostAdoptionProfile` is a trivially copyable, allocation-free version record.
It reports API contract versions only. It does not claim that every metadata
family is available for every container. Use `metadata_capability(...)` for
format/family support and inspect positional read completeness and diagnostics
for each asset.

## Stable V1 Boundary

| Contract | Stable surface |
| --- | --- |
| Positional source | `RandomAccessSource`, source ranges, caller-owned windows and accounting, exact reads, and `kRandomAccessSourceContractVersion` |
| Snapshot read | `read_transfer_source_snapshot_random_access(...)`, its scratch/options/result types, `complete()`, and `kReadTransferSourceSnapshotContractVersion` |
| Read diagnostics | `collect_read_transfer_source_diagnostics(...)`, caller-buffered diagnostic records and names/messages, and `kReadTransferSourceDiagnosticsContractVersion` |
| Snapshot state | The decoded-store `TransferSourceSnapshot` object contract and `kTransferSourceSnapshotContractVersion`; optional raw-carrier data may be preserved, but passthrough policy is not part of this profile |
| Snapshot persistence | `serialize_transfer_source_snapshot(...)`, `deserialize_transfer_source_snapshot(...)`, bounded transactional parsing, and canonical wire format v1 |
| Flat host reconciliation | Stable `FlatHost + ExportNamePolicy::Spec` export names plus typed add/update/remove import, tombstones, transactionality, and `kFlatHostImportContractVersion` |
| Codec operation schema | `TransferAdapterOpKind`, `PreparedTransferAdapterOp`, explicit target fields, and `kPreparedTransferAdapterContractVersion` |

Breaking any stable v1 shape or semantic requires a new contract version, a
compatibility path, or an ABI-major transition. Format support may grow without
changing the profile when existing result codes, limits, ownership, ordering,
and residual semantics remain compatible.

## Supported Reconciliation Sequence

The stable state path is:

1. Read through `read_transfer_source_snapshot_random_access(...)`.
2. Require `result.complete()` when the workflow needs complete metadata, or
   collect and apply policy to structured diagnostics.
3. Serialize the snapshot when it must outlive source storage or cross a host
   object/process boundary.
4. Deserialize transactionally.
5. Export `FlatHost` names with `ExportNamePolicy::Spec` and retain source entry
   identities in the host.
6. Import changed values, explicit typed additions, and removals. Replace the
   snapshot store only after `FlatHostImportResult::ok()`.
7. Pass the reconciled snapshot to `prepare_transfer_handoff(...)` for stable
   target-specific typed operations.

Import keeps source entry positions stable, represents removals as
`Dirty | Deleted` tombstones, and appends additions. This preserves untouched
complex metadata, provenance, duplicate order, and snapshot raw-carrier entry
links.

## Deliberate Experimental Boundary

Host Adoption Profile v1 does not stabilize:

- low-level format-specific `scan_*_random_access(...)`, payload extraction, or
  decoder APIs;
- file-path, whole-byte-span, or `build_transfer_source_snapshot(...)` helpers;
- `prepare_metadata_for_target_snapshot(...)`, `PreparedTransferBundle`,
  adapter-view build/validation/emission, or bundle execution;
- prepared payload/package serialization;
- raw-carrier passthrough policy or broad byte-preserving carrier transfer.

The typed adapter operation schema is stable so codec integrations do not parse
route strings. The underlying bundle/view builders remain experimental; use
[Prepared Transfer Handoff v1](prepared_transfer_handoff.md) to keep those
internals behind a stable opaque owner and allocation-free replay contract.

## Companion Target Handoff

Prepared Transfer Handoff v1 is a separate versioned contract because it was
added after the fixed Host Adoption Profile v1 descriptor. Check
`prepared_transfer_handoff_contract_version()` independently. Together, the
two contracts cover positional read, persisted/reconciled snapshot state, and
prepare-once typed encoder operations without changing the Profile v1 ABI.
Hosts using per-worker fixed-width updates must also check
`prepared_transfer_handoff_instance_contract_version()`.

## Performance And Concurrency

Positional callbacks are synchronous and must not throw. OpenMeta performs no
hidden retry, locking, scheduling, or whole-file materialization in the stable
positional read path. Scratch, read windows, limits, and accounting are caller
owned. The returned snapshot owns its finalized metadata state.

Independent operations may share an immutable source only when the host sets
`RandomAccessSource::concurrent_reads` and the backing implementation supports
concurrent exact reads. Do not share mutable scratch, accounting, import
results, prepared bundles, or writers between concurrent operations. An
immutable `PreparedTransferHandoff` supports concurrent const access with
independent callback state. Independently owned
`PreparedTransferHandoffInstance` objects may patch and replay concurrently;
one instance requires exclusive access while patching.
