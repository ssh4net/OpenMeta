Host Adoption Profile
=====================

Host Adoption Profile v1 is the narrow compatibility boundary for applications
that integrate OpenMeta into high-throughput image processing or transcoding.
It is host-neutral: the source may be memory, a file handle, a range-backed
object, or another synchronous positional I/O service.

Runtime contract check
----------------------

Include ``openmeta/host_adoption.h`` and compare the application header with the
linked library:

.. code-block:: cpp

   #include "openmeta/host_adoption.h"

   if (!openmeta::host_adoption_profile_matches(
           openmeta::kHostAdoptionProfileV1)) {
       // Reject the incompatible runtime before processing assets.
   }

``HostAdoptionProfile`` is a trivially copyable, allocation-free version record.
It reports API contract versions, not format coverage. Use
``metadata_capability(...)`` for format/family support and inspect positional
read completeness and diagnostics for each asset.

Stable v1 boundary
------------------

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Contract
     - Stable surface
   * - Positional source
     - ``RandomAccessSource``, ranges, caller-owned windows/accounting, exact
       reads, and ``kRandomAccessSourceContractVersion``.
   * - Snapshot read
     - ``read_transfer_source_snapshot_random_access(...)``, its
       scratch/options/result types, ``complete()``, and
       ``kReadTransferSourceSnapshotContractVersion``.
   * - Read diagnostics
     - ``collect_read_transfer_source_diagnostics(...)``, caller-buffered
       records and names/messages, and
       ``kReadTransferSourceDiagnosticsContractVersion``.
   * - Snapshot state and persistence
     - The decoded-store ``TransferSourceSnapshot`` contract and
       ``kTransferSourceSnapshotContractVersion`` plus bounded,
       transactional ``serialize_transfer_source_snapshot(...)`` and
       ``deserialize_transfer_source_snapshot(...)`` using canonical wire v1.
   * - Flat host reconciliation
     - Stable ``FlatHost + ExportNamePolicy::Spec`` export names plus typed
       add/update/remove import, tombstones, transactionality, and
       ``kFlatHostImportContractVersion``.
   * - Codec operation schema
     - ``TransferAdapterOpKind``, ``PreparedTransferAdapterOp``, explicit target
       fields, and ``kPreparedTransferAdapterContractVersion``.

Breaking a stable v1 shape or semantic requires a new contract version, a
compatibility path, or an ABI-major transition. Format support may grow when
existing result, limit, ownership, ordering, and residual semantics remain
compatible.

Supported reconciliation sequence
---------------------------------

1. Read with ``read_transfer_source_snapshot_random_access(...)``.
2. Require ``result.complete()`` when the workflow needs complete metadata, or
   collect and apply policy to structured diagnostics.
3. Serialize when the snapshot must outlive source storage or cross a host
   object/process boundary.
4. Deserialize transactionally.
5. Export ``FlatHost`` names with ``ExportNamePolicy::Spec`` and retain source
   entry identities.
6. Import changed values, explicit typed additions, and removals. Replace the
   snapshot store only after ``FlatHostImportResult::ok()``.
7. Pass the reconciled snapshot to target preparation.

Import preserves entry positions, represents removals as ``Dirty | Deleted``
tombstones, and appends additions. Untouched complex metadata, provenance,
duplicate order, and raw-carrier entry links remain intact.

Deliberate experimental boundary
--------------------------------

Profile v1 does not stabilize:

- low-level format-specific scanners, payload extractors, or decoders;
- file-path, whole-byte-span, or ``build_transfer_source_snapshot(...)`` helpers;
- ``prepare_metadata_for_target_snapshot(...)``, ``PreparedTransferBundle``,
  adapter-view build/validation/emission, or bundle execution;
- prepared payload/package serialization;
- raw-carrier passthrough policy or broad byte-preserving carrier transfer.

The typed operation schema is stable so codecs do not parse route strings, but
its current builders remain coupled to the experimental prepared-bundle model.
Hosts using target preparation should pin and test an OpenMeta release until
that construction boundary is stabilized separately.

Performance and concurrency
---------------------------

Positional callbacks are synchronous and must not throw. The stable positional
path has no hidden retry, locking, scheduling, or whole-file materialization.
Scratch, windows, limits, and accounting are caller owned; the returned
snapshot owns its finalized metadata state.

Independent operations may share an immutable source only when
``concurrent_reads`` is true and the backing supports concurrent exact reads.
Do not share mutable scratch, accounting, import results, prepared bundles, or
writers between concurrent operations.
