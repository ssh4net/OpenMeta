# Migrating to OpenMeta 0.5.0

Version 0.5.0 introduces a breaking prepared-patch API. Applications using the
old interface must use an earlier release or migrate and rebuild. No legacy
header aliases, old patch symbols or compatibility wrappers are provided.

| Before 0.5 | Since 0.5.0 |
| --- | --- |
| `openmeta/exif_tiff_patch.h` | `openmeta/metadata_patch.h` |
| `ExifTiffPatch*` types | `MetadataPatch*` types |
| `PreparedExifTiffPatchPlan` / `PreparedExifTiffPatchInstance` | `PreparedMetadataPatchPlan` / `PreparedMetadataPatchInstance` |
| `prepare_exif_tiff_patch_plan` | `prepare_metadata_patch_plan` |
| `create_prepared_exif_tiff_patch_instance` | `create_prepared_metadata_patch_instance` |
| `patch_prepared_exif_tiff_instance` | `patch_prepared_metadata_instance` |
| `exif_tiff_patch_contract_version` | `metadata_patch_contract_version` |
| `exif_tiff_patch_code_name` | `metadata_patch_code_name` |
| `options.serialization` | `options.exif`; store validation uses `options.validate` |
| `payload()` | `payload(MetadataPatchPayload::ExifTiff)` or `payload(MetadataPatchPayload::Xmp)` |

EXIF request/value shapes retain their typed meaning. Add XMP requests by emitted
namespace URI and simple property path, with an exact `escaped_width`. Mixed
EXIF/XMP batches are atomic across both payloads. Keep numeric TIFF updates typed;
XMP updates accept logical text and preserve its spelling. See
[canonical_patching.md](canonical_patching.md) for an example.

Set the new required `options.plan_id` to a fresh host-issued nonzero 48-bit ID
for each successful preparation. The host coordinates ID assignment and must
not reuse an ID while an old plan, worker or handle using it remains usable.
Default options with ID zero fail with `InvalidOptions`. The patch API has no
internal atomics, mutexes or global ID allocator; the host synchronizes shared
objects, borrowed payload access and lifetimes.

Patch handles belong to the supplied preparation generation. Rebuilding an identical
packet does not make previous handles valid for the new plan. Old instances
remain usable with their own handles even after the original plan is destroyed.
Handles and worker state are process-local and are not snapshot serialization
content.

## Build and binary boundary

The C++ shared-library ABI advances from 2 to 3. `OpenMeta_ABI_VERSION` in the
installed package reports 3; it is release-controlled rather than a cache
option. ELF uses the ABI-3 SONAME, macOS uses ABI 3, and Windows uses
`openmeta-3.dll`. The Windows import archive remains `openmeta_shared.lib`.
Static consumers and Python/native extensions must also rebuild against the
new headers and libraries. Use separate install prefixes when retaining an
older SDK alongside 0.5.0.

CMake package compatibility now requires the same major and minor version.
`find_package(OpenMeta 0.4 CONFIG REQUIRED)` does not accept a 0.5 package.
Migrated consumers can request `find_package(OpenMeta 0.5 CONFIG REQUIRED)`.
A consumer that specifies no version accepts responsibility for the selected SDK.

## Existing reader and codec integrations

This release does not require a change to the read/export/snapshot profile.
`HostAdoptionProfileV1` remains a separate reader integration contract; the new
patch API has its own version query. Existing prepared-transfer codec operations
remain available and continue to own their target-specific behavior.

Compared with 0.4.141, the previous 0.4.142 release already changed boundary
whitespace preservation for six camera/lens/spectral XMP fields. That observable
readback change is separate from the prepared-patch refactor.
