# Host Integration

This guide is for applications that already own the image/container side and
want OpenMeta to handle metadata.

OpenMeta is not an image encoder. The usual pattern is:

- decode metadata from one source file
- query or edit it in `MetaStore`
- hand prepared metadata to your own writer, encoder, or SDK

If you want the shortest end-to-end examples first, start with
[quick_start.md](quick_start.md).
For public API adoption status, see [api_stability.md](api_stability.md).
For the narrow stable realtime/transcoding boundary, see
[host_adoption_profile.md](host_adoption_profile.md).
For stable target preparation and typed operation replay, see
[prepared_transfer_handoff.md](prepared_transfer_handoff.md).
For the stable flat host naming contract, see
[flat_host_mapping.md](flat_host_mapping.md).
For deterministic host compatibility baselines, see
[compatibility_dump.md](compatibility_dump.md).
For generated XMP merge and writeback precedence, see
[xmp_sync_policy.md](xmp_sync_policy.md).
For per-target writer preserve/replace guarantees, see
[writer_target_contract.md](writer_target_contract.md).

## Pick The Integration Path

Use the narrowest public API that matches your host:

| Host owns | Use |
| --- | --- |
| Existing target file or template | `execute_prepared_transfer_file(...)` + `persist_prepared_transfer_file_result(...)` |
| EXR writer | `build_exr_attribute_batch_from_file(...)` |
| Host-owned metadata object model | `visit_metadata(...)` |
| Host metadata inspection/search UI | `openmeta/metadata_query.h` focused query helpers |
| Structured interpreted metadata records | `openmeta/metadata_interpretation.h` |
| Cross-family concept conflicts | `openmeta/metadata_concepts.h` |
| User-facing orientation display | `openmeta/orientation.h` |
| Common EXIF/TIFF/DNG and selected MakerNote value labels | `openmeta/exif_value_names.h` |
| JPEG/TIFF/DNG/JXL/WebP/PNG/JP2/BMFF/EXR encoder path from a snapshot | `prepare_transfer_handoff(...)` + indexed operation views or callback replay |
| Experimental file-based encoder preparation | `prepare_metadata_for_target_file(...)` + adapter view or backend emitter |
| Adobe DNG SDK objects/files | `dng_sdk_adapter.h` |

For inspection/search UI, prefer the experimental semantic query helpers before
building a separate fuzzy layer. They report source entries, confidence, value
shape, exact/fuzzy match provenance, and normalized candidates while preserving
ambiguity.

For host code that wants a simpler iterable result, use
`metadata_interpretation.h`. It keeps the same semantic vocabulary as query but
returns structured records with query class, normalized shape, source entries,
confidence, and normalized geometry/value arrays.

For host code that needs to reconcile duplicated concepts across metadata
families, use `metadata_concepts.h`. It reports orientation, date/time,
exposure/gain, color/profile, GPS, geometry, lens-correction, and
RAW-processing candidates with source families, preferred entries, and
same-role conflict flags. Exposure candidates expose capture facts such as
exposure time, aperture, ISO, exposure bias, exposure program, and gain as
safe transfer facts, while raw/DNG exposure adjustment fields remain unsafe for
rendered-image transfer. Geometry candidates expose crop, active-area, border,
and sensor-geometry roles with canonical origin, size, rect, and margin fields
when available, including known DNG, Phase One/Leaf, Fujifilm RAF, Canon, Nikon
Capture, and Sony panorama geometry patterns. Color/white balance,
lens-correction, and RAW-processing candidates expose full normalized value
vectors for grouped matrix/vector/table records when the source payloads
satisfy conservative numeric shape checks. Malformed or text-only source
records remain visible as individual metadata, but they are not promoted into
normalized grouped color, white-balance, or lens-correction candidates.
Date/time candidates include parsed date/time fields when the source value is
recognizable, plus precision and timezone-kind fields. Matching EXIF
`OffsetTime*` and `SubSecTime*` companions are assembled with their
`DateTime*` values; normalized subseconds are bounded to nine decimal digits.
Offset-aware candidates are compared as UTC instants, so equivalent timestamps
with different written offsets do not conflict. IPTC created date/time, IPTC
digital-creation date/time, XMP digitized timestamps, and GPS timestamps are
assembled where the source fields provide enough pieces. GPS timestamps
combine `GPSDateStamp` with `GPSTimeStamp` only within the same XMP property
scope, and GPS altitude candidates report whether `GPSAltitudeRef` marked the
height as below sea level. Camera position accepts XMP coordinates only from
the EXIF XMP schema. EXIF/XMP destination coordinates and IPTC Extension
`LocationShown` / `LocationCreated` coordinates use distinct roles, so they are
not conflated with camera position. Structured-location candidates carry a
`location_scope` such as `LocationShown[1]`; conflicts and preference are
resolved independently within each scope. Use
`metadata_concept_gps_altitude_reference_name(...)` for a stable display token.
Descriptive concepts reconcile standard EXIF, IPTC IIM, Dublin Core,
XMP Rights, Photoshop, IPTC Core/Extension, and PLUS title/headline,
description, creator, keyword/subject, location, copyright, rights/license,
credit, source, creator-contact, event, person, organization, product,
artwork/object, encoded-rights-expression, license-constraint, release,
legacy editorial, accessibility, taxonomy, resource/document identity,
controlled-vocabulary terms, registry entries, image-region entities,
document lineage/history, end-user, image-creator, image-supplier, and
delivered-image fields. Scalar
conflicts are isolated by normalized language and structured location scope;
additive collections retain distinct values. Structured
members carry both a `record_kind` and `record_scope`, such as `person` plus
`Person[1]`, so hosts can keep associated names, identifiers, descriptions,
contacts, constraints, and release records together.
IPTC image boundaries use the `image_region_boundary` record kind. A complete
boundary adds a `region_boundary` candidate with explicit
`image_region_shape` and `image_region_coordinate_unit` fields. Rectangle
values use `rect` as x, y, width, height; circles use a three-value x, y,
radius vector; polygons use a flattened x, y `vector_set` plus scoped `vec2`
vertex candidates. OpenMeta does not synthesize geometry when the shape, unit,
or required coordinates are incomplete, but valid individual numeric fields
remain queryable. Pixel and relative coordinates are not interchangeable:
hosts must use source and target image specifications to transform or validate
them before transfer.
Treat this as an inspection and policy input rather than an automatic metadata
rewrite decision; source-bound color, lens, and RAW-processing values still need
rendered-transfer safety filtering. Document identity, registry, XMP Media
Management manifest/version/lineage/history records, and pantry identity are
source-bound; image-region records require target image specifications because
their meaning is tied to the source image. Each candidate also carries a transfer
hint: `safe`, `source_bound`, `rendered_unsafe`, or
`requires_target_image_spec`, plus `compatible_file_safe` and
`rendered_image_safe` booleans for host UI and preflight policy. For transfer
previews, `transfer_concept_diagnostics_from_store(...)` converts those hints
into keep/drop/requires-target-image-spec actions for a selected
`TransferSafetyMode`, plus stable severity tokens, summary tokens, message
tokens, argument tokens, and default message text for host UI. If the host
knows the source storage context, pass
`MetadataRawDataDescriptor` to the descriptor-aware overload so RAW-processing
diagnostics distinguish stored RAW samples from rendered pixels before
choosing keep/drop actions. Rendered-transfer drop messages distinguish source
color transforms, white balance, lens correction, source RAW curves/linearity
metadata, source-bound RAW processing, and target-owned image properties.

`sensitivity` is a separate policy signal with `none`, `personal_contact`,
`person_identity`, `location`, and `legal_rights` values. A candidate marked
`safe` can still be privacy- or policy-sensitive. Hosts that publish or share
images should review or remove sensitive metadata according to their own user
consent, privacy, and rights policies; OpenMeta does not drop it automatically
merely because it is sensitive.
Hosts can localize or replace the wording, but they do not need to invent the
basic safe/drop/rewrite reasons.

## Adapter Classes

OpenMeta splits host integration surfaces deliberately:

- export-only naming/traversal surface:
  `visit_metadata(...)` for host-owned metadata mapping layers
- export-only adapter:
  `build_ocio_metadata_tree(...)` for OCIO-style metadata trees
- host-apply adapter:
  `build_exr_attribute_batch(...)` for EXR/OpenEXR header workflows
- direct bridge:
  `dng_sdk_adapter.h` for applications that already use Adobe DNG SDK objects
- narrow translator:
  `libraw_adapter.h` for orientation mapping into LibRaw flip space
- orientation utility:
  `orientation.h` for EXIF/TIFF labels, rotation degrees, mirrored-state
  checks, and width/height-swap checks
- value-name utility:
  `exif_value_names.h` for common EXIF/TIFF/DNG enum-style numeric labels and
  selected bounded Canon/Nikon/Sony/Fujifilm/Pentax/Olympus/Panasonic/
  Phase One/Kodak/Minolta/Sigma/Samsung/Ricoh MakerNote labels
- structured interpretation utility:
  `metadata_interpretation.h` for query-backed semantic records
- concept-resolution utility:
  `metadata_concepts.h` for cross-family orientation, date/time, color/profile,
  exposure/gain, GPS, descriptive fields, geometry, lens-correction, and
  RAW-processing conflict inspection

## 1. Read Into `MetaStore`

```cpp
#include "openmeta/simple_meta.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

std::vector<std::byte> file_bytes = load_file_somehow("input.jpg");

openmeta::MetaStore store;
std::array<openmeta::ContainerBlockRef, 256> blocks {};
std::array<openmeta::ExifIfdRef, 512> ifds {};
std::vector<std::byte> payload(1 << 20);
std::array<uint32_t, 1024> payload_indices {};

openmeta::SimpleMetaDecodeOptions options;
openmeta::SimpleMetaResult result = openmeta::simple_meta_read(
    std::span<const std::byte>(file_bytes.data(), file_bytes.size()),
    store,
    blocks,
    ifds,
    payload,
    payload_indices,
    options);

store.finalize();
```

The caller owns the scratch buffers. That is deliberate: the API stays
deterministic and easy to reuse in hot paths.

## 2. Query By Exact Key

```cpp
#include "openmeta/meta_key.h"

openmeta::MetaKeyView key;
key.kind = openmeta::MetaKeyKind::ExifTag;
key.data.exif_tag.ifd = "ifd0";
key.data.exif_tag.tag = 0x010F;  // Make

for (openmeta::EntryId id : store.find_all(key)) {
    const openmeta::Entry& entry = store.entry(id);
    // Inspect entry.value and entry.origin.
}
```

Use exact lookup for deterministic key access. For inspection/search UI, prefer
`openmeta/metadata_query.h` before building a separate layer. It returns source
entries, confidence, value shape, and normalized candidates. Semantic Query
uses built-in tags and conservative namespace/name rules; it intentionally
does not use partial matching because near names must not alter classification.
Raw matches retain `exact_match`, `fuzzy_match`, and `fuzzy_score`
compatibility fields.

For a free-text search box, use `openmeta/metadata_fuzzy_search.h` instead of
merging ranking policy into semantic Query. `fuzzy_search_metadata(...)`
searches decoded names and property paths with explicit score and result
bounds. It returns deterministic top-k results with exact, alias, or fuzzy
provenance and stable entry-id tie-breaking. The current normalization contract
is locale-independent ASCII; non-ASCII queries return
`UnsupportedQueryText`, with no implicit transliteration. Thin Python
`Document` and `TransferSourceSnapshot` wrappers return the same status,
counts, truncation flag, and match fields. Calls keep local state and may run
concurrently against an immutable finalized store. The current bounded linear
scan is intended for ordinary per-image stores; see
[fuzzy_search.md](fuzzy_search.md) for benchmark results and the deferred
immutable-index boundary.

## 3. Generic Host Metadata Traversal

Use the traversal API when your application owns the metadata object model and
needs deterministic exported names plus the original `Entry`.

```cpp
#include "openmeta/interop_export.h"

class MyMetadataSink final : public openmeta::MetadataSink {
public:
    void on_item(const openmeta::ExportItem& item) noexcept override
    {
        // Map item.name + item.entry into your host metadata object.
    }
};

openmeta::ExportOptions options;
options.style              = openmeta::ExportNameStyle::FlatHost;
options.name_policy        = openmeta::ExportNamePolicy::Spec;
options.include_makernotes = false;

MyMetadataSink sink;
openmeta::visit_metadata(store, options, sink);
```

This keeps host-specific object ownership and write behavior outside OpenMeta.
Use `ExportNamePolicy::Spec` when the host's tables use specification names such
as `Exif:ISOSpeedRatings` and `Exif:ExposureBiasValue`. The default
`ExifToolAlias` policy is intended for ExifTool-compatible display/parity names.

For writeback, `import_flat_host_metadata(...)` accepts ordered typed values and
returns a detached finalized `MetaStore`. Existing values may be selected by the
source `EntryId`, or by name only when that `FlatHost` name is unique. Duplicate
names are intentionally ambiguous. New custom XMP, EXIF, IPTC, or other entries
must include an explicit `MetaKeyView`; OpenMeta does not guess a namespace or
numeric tag from a lossy flat name. `RemoveSourceEntry` and `RemoveUniqueName`
requests use an empty value and retain a `Dirty | Deleted` tombstone. This keeps
entry ids, provenance, and snapshot raw-carrier links stable while subsequent
export and transfer omit the deleted metadata.

### Random-Access Source Foundation

For realtime and large-asset pipelines, `openmeta/random_access_source.h`
defines a borrowed fixed-size source with either caller-owned contiguous memory
or an exact synchronous `read_at(offset, destination)` callback. The primitive
has no hidden allocation, lock, scheduler, or file-position state. Request
count, total-byte, and single-read ceilings are tracked in caller-owned state.

Version 0.4.101 routes classic TIFF, BigTIFF, DNG, RW2, and ORF header/IFD
decoding through this contract with caller-owned structural and value scratch.
The existing span API is a zero-copy memory-source adapter over the same public
entry point. Callback-backed PrintIM, GeoTIFF, Pentax DNG private data, and
selected self-contained MakerNotes are supported. Version 0.4.102 adds
source-backed Nikon embedded TIFF/type 1, Sony outer-TIFF-relative IFDs, and
contained Canon adjusted-base payloads. Version 0.4.103 adds legacy and modern
Olympus nested IFDs, Panasonic binary tables, and self-contained Samsung STMN
and Type2 derived tables. Version 0.4.104 adds bounded Fujifilm self-relative
IFDs and General Imaging Type 2 source windows. Version 0.4.105 adds Kodak
fixed-layout records and outer-TIFF-relative Type 8, Type 10, and Type 11 IFDs
with vendor subtables. Version 0.4.106 adds Ricoh, Nintendo, Casio, Minolta, and
FLIR callback parity. Version 0.4.107 adds bounded leading JPEG metadata-segment
scanning with 512-byte caller scratch and no entropy-data reads. Version 0.4.108
adds positional PNG/WebP chunk scanning and JP2/JXL/ISO-BMFF structural and
metadata-item traversal while skipping image/media payloads. Version 0.4.109
adds positional GIF extension scanning, EXR header traversal, bounded logical
payload fetching, and decoded snapshot assembly through
`read_transfer_source_snapshot_random_access(...)`. Scanner, decoder, and
payload workspaces remain caller-owned; the returned snapshot owns its finalized
store. Aggregate source limits cover every phase, and ordinary decoded assembly
does not retain the whole file. Version 0.4.111 adds native positional RAF,
X3F, and CRW/CIFF metadata traversal and allocation-free structured read
diagnostics. Version 0.4.112 adds bounded traversal and decode of declared RAF
preview-JPEG/FujiIFD metadata and X3F section-JPEG metadata without reading
JPEG entropy or RAW image payloads. Unconverted Canon derived subtables outside the
declared payload and unknown vendor layouts remain explicit residuals through
`ExifRandomAccessDecodeResult::nested_payloads_skipped`; hosts must check
`complete()` before claiming full nested parity. Undeclared source-wide
fallback searches, selected BMFF enrichment, and whole-file raw carriers also
remain explicit positional residuals. Use
`collect_read_transfer_source_diagnostics(...)` to project bounded severity,
code, family, source-offset/sizing, MakerNote, and residual details. See
[random_access_input.md](random_access_input.md) for buffer sizing, lifetime,
short-read, concurrency, and performance requirements.

## 4. Build An EXR Attribute Batch

This is the cleanest host-adapter path in OpenMeta today.

```cpp
#include "openmeta/exr_adapter.h"

openmeta::ExrAdapterBatch batch;
openmeta::BuildExrAttributeBatchFileOptions options;

openmeta::BuildExrAttributeBatchFileResult result =
    openmeta::build_exr_attribute_batch_from_file(
        "source.jpg", &batch, options);

for (const openmeta::ExrAdapterAttribute& attr : batch.attributes) {
    // Forward attr.name, attr.type_name, and attr.value to your EXR writer.
}
```

OpenMeta does not need OpenEXR headers for this path. It exports a neutral
batch of EXR-style attributes that your host can apply through OpenEXR or its
own EXR writer.

## 5. Feed A Host-Owned JPEG Or JXL Encoder

There are three public patterns for encoder-owned output:

- prepare a stable opaque handoff and replay typed operations
- implement a backend emitter such as `JpegTransferEmitter` or
  `JxlTransferEmitter`
- use the lower-level experimental bundle/adapter view APIs

### Stable Handoff Pattern

Use this when the host already has a decoded `TransferSourceSnapshot` and owns
the target encoder:

```cpp
#include "openmeta/prepared_transfer_handoff.h"

openmeta::TransferStatus emit_to_codec(
    void* codec,
    const openmeta::PreparedTransferHandoffOperationView* view) noexcept
{
    // Dispatch on view->operation.kind and use the explicit target fields.
    // EXR uses view->exr_name/type/value; other targets use view->payload.
    return openmeta::TransferStatus::Ok;
}

openmeta::PrepareTransferRequest request;
request.target_format = openmeta::TransferTargetFormat::Jxl;

openmeta::PreparedTransferHandoff handoff;
openmeta::PreparedTransferHandoffResult prepared =
    openmeta::prepare_transfer_handoff(
        snapshot, request, openmeta::EmitTransferOptions {}, &handoff);
if (prepared.ok()) {
    openmeta::replay_prepared_transfer_handoff(
        handoff, emit_to_codec, codec);
}
```

Preparation owns allocation and route compilation. Successful operation access
and replay are allocation-free and can be repeated without rebuilding the
operation vector. See [prepared_transfer_handoff.md](prepared_transfer_handoff.md)
for lifetime, concurrency, and deliberate-boundary details.

For realtime outputs with changing fixed-width time fields, create one
`PreparedTransferHandoffInstance` per worker from the immutable handoff. Worker
creation may allocate; strict transactional patching and replay do not:

```cpp
openmeta::PreparedTransferHandoffInstance worker;
openmeta::create_prepared_transfer_handoff_instance(handoff, &worker);

constexpr char encoded_time[] = "2030:12:31 23:59:59";
const openmeta::TimePatchView patch {
    openmeta::TimePatchField::DateTime,
    std::as_bytes(std::span<const char>(encoded_time, sizeof(encoded_time)))
};
openmeta::patch_prepared_transfer_handoff_instance(
    &worker, std::span<const openmeta::TimePatchView>(&patch, 1U));
openmeta::replay_prepared_transfer_handoff_instance(
    worker, emit_to_codec, codec);
```

Patch values are serialized bytes. EXIF ASCII date/time values include the
terminating NUL, so `encoded_time` above has the required 20-byte width. Do not
share one mutable instance across workers; independent instances own separate
payload storage. Generic hosts can call
`prepared_transfer_handoff_instance_time_patch_field(...)` during setup to
obtain the exact width and slot count without exposing payload offsets.

### Target-Neutral Canonical EXIF Patching

Use `openmeta/exif_tiff_patch.h` when the destination container is host-owned or
not known during metadata preparation. It compiles exact EXIF key occurrences
into opaque handles over unwrapped canonical TIFF bytes. Preparation and worker
creation may allocate; typed fixed-width patch batches and `payload()` replay do
not. The host adds JPEG, PNG/WebP, JP2/JXL/BMFF, JPH, or private-container
framing after patching. See
[canonical_patching.md](canonical_patching.md) for the transaction, concurrency,
and supported-type contract.

### Experimental Adapter-View Pattern

Use this when you want one target-neutral operation list.

```cpp
#include "openmeta/metadata_transfer.h"

class MySink final : public openmeta::TransferAdapterSink {
public:
    openmeta::TransferStatus
    emit_op(const openmeta::PreparedTransferAdapterOp& op,
            std::span<const std::byte> payload) noexcept override
    {
        // Dispatch on op.kind and forward payload into your backend.
        return openmeta::TransferStatus::Ok;
    }
};

openmeta::PrepareTransferFileOptions prepare;
prepare.prepare.target_format = openmeta::TransferTargetFormat::Jxl;

openmeta::PrepareTransferFileResult prepared =
    openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

openmeta::PreparedTransferAdapterView view;
openmeta::build_prepared_transfer_adapter_view(
    prepared.bundle, &view, openmeta::EmitTransferOptions {});
openmeta::validate_prepared_transfer_adapter_view(prepared.bundle, view);

MySink sink;
openmeta::emit_prepared_transfer_adapter_view(prepared.bundle, view, sink);
```

This is a good fit when your host already has its own abstraction for
"metadata op + bytes". `kPreparedTransferAdapterContractVersion` versions the
codec-facing operation schema independently from internal route strings.
The operation schema is stable v1; adapter-view construction, validation, and
emission remain experimental because they expose the prepared-bundle model.

| Operation kind | Insertion fields |
| --- | --- |
| `JpegMarker` | `jpeg_marker_code` and marker payload |
| `TiffTagBytes` | `tiff_tag` and tag value bytes |
| `JxlBox` / `JxlIccProfile` | `box_type`, `compress`, or direct ICC payload |
| `WebpChunk` / `PngChunk` | `chunk_type` and chunk payload |
| `Jp2Box` | `box_type` and box payload |
| `BmffItem` / `BmffProperty` | item/property type, subtype, MIME-XMP flag, and payload |
| `ExrAttribute` | call `get_prepared_transfer_adapter_exr_attribute_view(...)` for borrowed name, type, and value |

`validate_prepared_transfer_adapter_view(...)` rebuilds the canonical operation
list and compares every kind-specific field. Hosts should validate cached or
cross-layer views before codec handoff. Payload spans passed to
`TransferAdapterSink::emit_op(...)` are borrowed for that call. The typed EXR
view borrows from the unchanged source bundle.

### Backend-Emitter Pattern

Use this when your host already looks like one OpenMeta backend.

```cpp
#include "openmeta/metadata_transfer.h"

class MyJpegEmitter final : public openmeta::JpegTransferEmitter {
public:
    openmeta::TransferStatus
    write_app_marker(uint8_t marker_code,
                     std::span<const std::byte> payload) noexcept override
    {
        // Write one APPn marker into your JPEG output path.
        return openmeta::TransferStatus::Ok;
    }
};

openmeta::PrepareTransferFileOptions prepare;
prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;

openmeta::PrepareTransferFileResult prepared =
    openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

openmeta::PreparedTransferExecutionPlan plan;
openmeta::compile_prepared_transfer_execution(
    prepared.bundle, openmeta::EmitTransferOptions {}, &plan);

MyJpegEmitter emitter;
openmeta::emit_prepared_transfer_compiled(prepared.bundle, plan, emitter);
```

For JXL, implement `JxlTransferEmitter::set_icc_profile(...)`,
`add_box(...)`, and `close_boxes(...)`.

OpenMeta does not ship a TurboJPEG-specific wrapper yet. The intended
integration path is still through `JpegTransferEmitter` or the adapter view.

## 6. Edit An Existing Target File

If your host already has a target file or template on disk, use the file
helper instead of building your own writer path.

```cpp
#include "openmeta/metadata_transfer.h"

openmeta::ExecutePreparedTransferFileOptions exec_options;
exec_options.prepare.prepare.target_format =
    openmeta::TransferTargetFormat::Tiff;
exec_options.edit_target_path = "rendered.tif";

openmeta::ExecutePreparedTransferFileResult exec =
    openmeta::execute_prepared_transfer_file("source.jpg", exec_options);

openmeta::PersistPreparedTransferFileOptions persist;
persist.output_path = "rendered_with_meta.tif";
persist.overwrite_output = true;

openmeta::PersistPreparedTransferFileResult saved =
    openmeta::persist_prepared_transfer_file_result(exec, persist);
```

This path is usually simpler than a custom adapter when the container already
exists.

### Read Once, Save Later

If your host already decoded source metadata during the initial load, keep a
decoded source snapshot and execute the later save without reopening the
source file:

```cpp
#include "openmeta/metadata_transfer.h"

const openmeta::ReadTransferSourceSnapshotFileResult snapshot =
    openmeta::read_transfer_source_snapshot_file("source.jpg");

openmeta::ExecutePreparedTransferSnapshotOptions options;
options.prepare.target_format = openmeta::TransferTargetFormat::Tiff;
options.edit_target_path      = "target.tif";
options.execute.edit_apply    = true;

openmeta::ExecutePreparedTransferFileResult result =
    openmeta::execute_prepared_transfer_snapshot(
        snapshot.snapshot, options);
```

Python mirrors that same host-facing snapshot flow:

```python
from pathlib import Path

import openmeta

snapshot_info = openmeta.read_transfer_source_snapshot_file("source.jpg")
snapshot = snapshot_info["snapshot"]

result = openmeta.transfer_snapshot_file(
    snapshot,
    target_format=openmeta.TransferTargetFormat.Tiff,
    edit_target_path="target.tif",
    target_bytes=Path("target.tif").read_bytes(),
    output_path="edited.tif",
)
```

Current source snapshots are decoded-store-backed by default. They are intended
for the normal EXIF/XMP/ICC/IPTC transfer flow, where OpenMeta re-emits decoded
metadata after applying the selected safety policy. If a host needs source
carrier provenance for diagnostics or a later passthrough policy decision,
enable `ReadTransferSourceSnapshotFileOptions::preserve_raw_carriers` or pass
`ReadTransferSourceSnapshotOptions` with `preserve_raw_carriers` set. Each
raw carrier records its route, semantic kind, payload bytes, and snapshot-local
decoded entry ids attributed to that carrier.
Call `raw_carrier_passthrough_audit_from_snapshot(...)` before any host-owned
passthrough decision. The audit reports candidate carriers and primary block
reasons such as missing payload, target incompatibility, safety filtering,
content-bound C2PA, explicit profile policy, missing decoded entry links, or
unsupported carrier kind.
Python exposes the same check as
`snapshot.raw_carrier_passthrough_audit(...)`.
Snapshot preparation defaults to decoded re-emission. Hosts that need bounded
raw reuse can set `PrepareTransferRequest::raw_carrier_passthrough_mode` to
`TransferRawCarrierPassthroughMode::WhenSafe`, or pass
`raw_carrier_passthrough_mode=openmeta.TransferRawCarrierPassthroughMode.WhenSafe`
to Python snapshot transfer helpers. The current passthrough path is limited
to eligible non-C2PA JUMBF and OpenMeta draft unsigned C2PA invalidation
carriers for JPEG, JXL, and BMFF targets, plus draft unsigned C2PA
invalidation carriers for WebP. EXIF/XMP/ICC/IPTC remain decoded re-emitted.
For hosts that still own the bundle/execution split, the lower-level
`prepare_metadata_for_target_snapshot(...)` entry point remains available.
If the host already has a decoded `MetaStore`, build a reusable snapshot with
`build_transfer_source_snapshot(store)`. If it already owns the source bytes in
memory, use `read_transfer_source_snapshot_bytes(bytes)` instead of the
file-path reader.
In Python, a previously decoded `Document` can be turned into a reusable
snapshot through `doc.build_transfer_source_snapshot()` or
`openmeta.build_transfer_source_snapshot(doc)`.
If it also owns the destination bytes in memory, call the overload
`execute_prepared_transfer_snapshot(snapshot, target_bytes, options)`.
If it already holds a prepared bundle, use
`execute_prepared_transfer_bundle(bundle, target_bytes, options)` instead.
Snapshots can cross a process or host-object boundary without retaining their
source storage:

```cpp
std::vector<std::byte> persisted;
openmeta::serialize_transfer_source_snapshot(snapshot.snapshot, &persisted);

openmeta::TransferSourceSnapshot restored;
openmeta::deserialize_transfer_source_snapshot(persisted, &restored);
```

The parser is transactional and bounded by `TransferSourceSnapshotIoOptions`.
Serialization preserves raw carrier bytes only when they were captured in the
snapshot; it does not make those bytes safe to relocate or rewrite.
The canonical v1 wire representation is compatibility-locked. Current readers
accept v1 and reject unknown versions without changing the output snapshot.

### Reconcile Host Attributes After Deserialization

The supported deferred-edit sequence is:

1. Deserialize the source snapshot.
2. Compare the host's current FlatHost attributes with its saved export.
3. Import changed values, explicit additions, and removals.
4. Replace only the snapshot store after a successful transaction.
5. Prepare target-specific payloads from the reconciled snapshot.

```cpp
openmeta::TransferSourceSnapshot snapshot;
openmeta::deserialize_transfer_source_snapshot(persisted, &snapshot);

std::vector<openmeta::FlatHostImportItem> changes = host_changes();
openmeta::FlatHostImportOptions import_options;
import_options.name_policy = openmeta::ExportNamePolicy::Spec;

openmeta::FlatHostImportResult imported =
    openmeta::import_flat_host_metadata(snapshot.store, changes,
                                        import_options);
if (imported.ok()) {
    snapshot.store = std::move(imported.store);
}

openmeta::PrepareTransferRequest request;
request.target_format = openmeta::TransferTargetFormat::Webp;
openmeta::PreparedTransferHandoff handoff;
openmeta::prepare_transfer_handoff(
    snapshot, request, openmeta::EmitTransferOptions {}, &handoff);
```

Import preserves every source entry position and appends additions, so raw
carrier `decoded_entry_ids` remain valid. Tombstones preserve removed-entry
identity. Untouched complex metadata and optional raw carriers remain in the
snapshot; normal target safety and serialization policy still decides what is
emitted.
Snapshot execution supports the same existing-sidecar merge and destination
carrier-precedence controls as the file helper; when loading an existing
sidecar it defaults to `edit_target_path` unless
`xmp_existing_sidecar_base_path` is set explicitly.
For embedded-only writeback with sidecar cleanup and no filesystem path, set
`xmp_existing_destination_sidecar_state` explicitly so OpenMeta can return a
cleanup decision without guessing a sidecar location.
Python now exposes those same split path/state controls directly:
`xmp_existing_sidecar_base_path`, `xmp_sidecar_base_path`,
`xmp_existing_destination_embedded_path`, and
`xmp_existing_destination_sidecar_state`.

The CLI and Python command-line wrapper do not implement their own transfer
semantics. They map flags onto the same file-helper contract:
- `--output` is the sidecar base for `sidecar` and `embedded_and_sidecar`
  writeback, so the generated sidecar is `output-stem.xmp`.
- `--xmp-writeback sidecar` suppresses generated embedded XMP.
- `--xmp-writeback embedded_and_sidecar` writes generated XMP to both the
  edited output and the generated sidecar.
- embedded-only writeback preserves an existing destination sidecar unless
  `--xmp-destination-sidecar strip_existing` is selected.
- sidecar-only writeback preserves existing destination embedded XMP unless
  `--xmp-destination-embedded strip_existing` is selected.
- `--force` maps to the C++ persistence overwrite flags for the primary
  output and generated sidecar.

## 7. Query Runtime Capabilities

Hosts can ask OpenMeta what the current build supports before wiring format
menus, warnings, or integration feature flags.

```cpp
#include "openmeta/metadata_capabilities.h"

openmeta::MetadataCapability cap = openmeta::metadata_capability(
    openmeta::TransferTargetFormat::Avif,
    openmeta::MetadataCapabilityFamily::Xmp);

if (openmeta::metadata_capability_available(cap.target_edit)) {
    // The current build can edit AVIF XMP within the reported support level.
}
```

Each operation reports one of `unsupported`, `supported`, `bounded`, or
`disabled`. `bounded` means the capability exists within OpenMeta's documented
contract, not that it is arbitrary metadata-editor parity. `disabled` is used
for compile-time-disabled support such as XMP decode when XML support is not
available.

Python exposes the same query:

```python
cap = openmeta.metadata_capability(
    openmeta.TransferTargetFormat.Avif,
    openmeta.MetadataCapabilityFamily.Xmp,
)
print(cap["target_edit_name"])
```

## 8. Use The Optional Adobe DNG SDK Bridge

If OpenMeta was built with `OPENMETA_WITH_DNG_SDK_ADAPTER=ON`, you can use the
optional SDK bridge in two ways.

### Update An Existing DNG File

```cpp
#include "openmeta/dng_sdk_adapter.h"

openmeta::ApplyDngSdkMetadataFileResult result =
    openmeta::update_dng_sdk_file_from_file("source.jpg", "target.dng");
```

### Apply Onto Existing SDK Objects

```cpp
#include "openmeta/dng_sdk_adapter.h"
#include "openmeta/metadata_transfer.h"

openmeta::PrepareTransferFileOptions prepare;
prepare.prepare.target_format = openmeta::TransferTargetFormat::Dng;

openmeta::PrepareTransferFileResult prepared =
    openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

openmeta::DngSdkAdapterOptions adapter;
openmeta::apply_prepared_dng_sdk_metadata(
    prepared.bundle, host, negative, adapter);
```

This bridge is for applications that already use the Adobe DNG SDK. OpenMeta
still does not encode pixels or invent raw-image structure.

### Host-Owned Image Specs

If a transfer target is produced from a different image buffer than the source,
the host writer owns the target image facts: dimensions, channel count, sample
type, compression, orientation, colorspace, ICC profile, and TIFF strip/tile
storage. OpenMeta does not infer those values from copied metadata. During
prepared transfer it filters source EXIF/XMP image-layout fields so stale source
properties are not written into a different output image.

Host code that encodes pixels should keep those fields from the target
container or inject values derived from the actual output buffer. Enable source
ICC transfer only when the host has verified that the profile matches the target
pixel buffer; otherwise preserve or write the target profile.

Use `TransferProfile::safety` for the broad source/destination relationship:

| Mode | Use when | Transfer policy |
| --- | --- | --- |
| `CompatibleFile` | Metadata is repackaged or recompressed into a compatible file/pixel representation | Preserve source camera, color, ICC, and camera-specific data except target-owned image-layout fields |
| `RenderedImage` | Pixels may have changed, especially RAW-to-JPEG/PNG/WebP/JXL/HEIF/AVIF export | Keep general/time/GPS/IPTC/portable XMP; drop source raw color calibration, profile/gain tables, raw digests/storage identifiers, linearization/crop/correction metadata, vendor RAW/source-processing geometry/color/correction/thermal/computational/private/stitch fields, camera raw settings XMP, source ICC, opaque MakerNotes, and non-C2PA JUMBF |

See [writer_target_contract.md](writer_target_contract.md#transfer-safety-matrix)
for the detailed per-group transfer matrix.

```cpp
openmeta::PrepareTransferRequest request;
request.target_format = openmeta::TransferTargetFormat::Jpeg;
request.profile.safety = openmeta::TransferSafetyMode::RenderedImage;

request.target_image_spec.has_dimensions = true;
request.target_image_spec.width = encoded_width;
request.target_image_spec.height = encoded_height;

request.target_image_spec.has_samples_per_pixel = true;
request.target_image_spec.samples_per_pixel = 3;
request.target_image_spec.bits_per_sample_count = 1;
request.target_image_spec.bits_per_sample[0] = 8;
request.target_image_spec.has_photometric_interpretation = true;
request.target_image_spec.photometric_interpretation = 2; // RGB
request.target_image_spec.has_exif_color_space = true;
request.target_image_spec.exif_color_space = 1; // sRGB
```

Python exposes the same structure as `openmeta.TransferTargetImageSpec` and the
command-line wrappers pass it through without a separate policy layer:

```python
spec = openmeta.TransferTargetImageSpec()
spec.has_dimensions = True
spec.width = encoded_width
spec.height = encoded_height
spec.has_samples_per_pixel = True
spec.samples_per_pixel = 3
spec.bits_per_sample = [8]
spec.has_photometric_interpretation = True
spec.photometric_interpretation = 2
spec.has_exif_color_space = True
spec.exif_color_space = 1
```

For smoke testing the file-helper path, `metatransfer` and
`python -m openmeta.python.metatransfer` expose equivalent flags:

```bash
metatransfer --target-jpeg target.jpg -o output.jpg \
  --target-width 320 --target-height 240 \
  --target-samples-per-pixel 3 --target-bits-per-sample 8 \
  --target-photometric 2 --target-exif-color-space 1 \
  source.jpg
```

## 9. Query Phase One RAW Processing Metadata

After decoding MakerNotes, hosts can query Phase One/Leaf RAW processing data
without depending on private MakerNote tag layout. The helper reports presence
and normalized values for color matrices, WB RGB levels, black level, sensor
temperatures, raw-data/storage byte counts, and sensor-calibration summaries.
These values are source-RAW processing metadata; do not write them into rendered
outputs unless the destination is a compatible RAW-style target.

```cpp
#include "openmeta/phaseone_geometry.h"

openmeta::PhaseOneRawGeometryResult geometry =
    openmeta::phaseone_raw_geometry_from_store(store);
openmeta::PhaseOneRawProcessingResult raw =
    openmeta::phaseone_raw_processing_from_store(store);

if (raw.status == openmeta::PhaseOneRawProcessingStatus::Ok &&
    raw.info.has_color_matrix1) {
    const double m00 = raw.info.color_matrix1[0];
    (void)m00;
}
```

Python exposes the same normalized queries on decoded documents and reusable
transfer snapshots:

```python
doc = openmeta.read("source.iiq", decode_makernote=True)
geometry = doc.phaseone_raw_geometry()
raw = doc.phaseone_raw_processing()

if (raw["status"] == openmeta.PhaseOneRawProcessingStatus.Ok and
        raw["has_color_matrix1"]):
    m00 = raw["color_matrix1"][0]
```

The `metaread` command prints compact `phaseone_raw_geometry=...` and
`phaseone_raw_processing=...` summaries when those decoded fields are present.

## 10. Query Vendor RAW Processing Metadata

For Sony, Canon, Nikon, Fujifilm, Pentax, Panasonic, Olympus, Kodak, Minolta,
Sigma, Samsung, Ricoh, Apple, DJI, Google, FLIR, Casio, Sanyo, KyoceraRaw,
Reconyx, HP, JVC, GE, Motorola, Nintendo, and Microsoft, OpenMeta exposes a
conservative grouped summary instead of vendor-specific decoded values. The
helper reports whether decoded MakerNote fields look like source RAW color/WB,
geometry/storage, lens correction, raw-data, sensor-calibration,
computational, thermal, preview/face geometry, stitch/panorama geometry, or
vendor-private table metadata. Use it to audit transfer safety decisions and
host UI, not as a rendered-output write source.
The same classification feeds semantic query and interpretation records as
per-family grouped table/vector candidates when multiple related vendor fields
are present.

```cpp
#include "openmeta/vendor_raw_processing.h"

openmeta::VendorRawProcessingSummary sony =
    openmeta::vendor_raw_processing_from_store(
        store, openmeta::VendorRawProcessingFamily::Sony);

if (sony.fields_seen > 0) {
    const uint32_t unsafe_for_rendered = sony.color_fields +
        sony.white_balance_fields + sony.lens_correction_fields;
    (void)unsafe_for_rendered;
}

openmeta::TransferSafetyAudit audit =
    openmeta::transfer_safety_audit_from_store(
        store, openmeta::TransferSafetyMode::RenderedImage);

if (audit.filtered_raw_color_calibration > 0 ||
    audit.filtered_icc_profiles > 0 ||
    audit.filtered_makernotes > 0) {
    // Show the host/user which source-bound metadata will not be transferred.
}

openmeta::TransferConceptDiagnostics diagnostics =
    openmeta::transfer_concept_diagnostics_from_store(
        store, openmeta::TransferSafetyMode::RenderedImage);

openmeta::MetadataRawDataDescriptor raw_descriptor;
raw_descriptor.encoding = openmeta::MetadataRawDataEncoding::Rendered;
openmeta::TransferConceptDiagnostics descriptor_diagnostics =
    openmeta::transfer_concept_diagnostics_from_store(
        store, openmeta::TransferSafetyMode::CompatibleFile, raw_descriptor);

for (size_t i = 0U; i < diagnostics.diagnostics.size(); ++i) {
    const openmeta::TransferConceptDiagnostic& item =
        diagnostics.diagnostics[i];
    const char* action =
        openmeta::transfer_concept_diagnostic_action_name(item.action);
    const char* reason =
        openmeta::transfer_concept_diagnostic_reason_name(item.reason);
    const char* severity =
        openmeta::transfer_concept_diagnostic_severity_name(item.severity);
    const char* message =
        openmeta::transfer_concept_diagnostic_message(item);
    const std::string token =
        openmeta::transfer_concept_diagnostic_token(item);
    const std::string message_token =
        openmeta::transfer_concept_diagnostic_message_token(item);
    const std::vector<std::string> message_arguments =
        openmeta::transfer_concept_diagnostic_message_arguments(item);
    (void)action;
    (void)reason;
    (void)severity;
    (void)message;
    (void)token;
    (void)message_token;
    (void)message_arguments;
}
```

If a decoder exposes a curve/LUT metadata entry that only affects compressed RAW
storage, set `raw_descriptor.requires_compressed_raw_encoding = true` for the
descriptor-aware diagnostics call. OpenMeta will then treat that curve as not
applicable for uncompressed or packed raw buffers instead of assuming the
metadata is active.

If the decoder also knows that the curve/LUT only affects the primary raw
plane, set `raw_descriptor.requires_primary_raw_plane = true` and provide
`raw_descriptor.has_plane_index = true` plus `raw_descriptor.plane_index` for
the raw buffer being checked. Non-primary planes are then reported as not
applicable; unknown plane selection remains conditional.

Python uses the same family enum:

```python
summary = doc.vendor_raw_processing(openmeta.VendorRawProcessingFamily.Nikon)
if summary["fields_seen"]:
    print(summary["lens_correction_fields"])

audit = doc.transfer_safety_audit(openmeta.TransferSafetyMode.RenderedImage)
print(audit["filtered_raw_color_calibration"])

diagnostics = doc.transfer_concept_diagnostics(
    openmeta.TransferSafetyMode.RenderedImage
)
raw_descriptor = openmeta.MetadataRawDataDescriptor()
raw_descriptor.encoding = openmeta.MetadataRawDataEncoding.Rendered
descriptor_diagnostics = doc.transfer_concept_diagnostics(
    openmeta.TransferSafetyMode.CompatibleFile,
    raw_descriptor,
)
for item in diagnostics["diagnostics"]:
    print(
        item["kind_name"],
        item["role_name"],
        item["action_name"],
        item["severity_name"],
        item["token"],
        item["message_token"],
        item["message_arguments"],
        item["message"],
    )
```

`metaread` prints
`vendor_raw_processing[sony|canon|nikon|fujifilm|pentax|panasonic|olympus|kodak|minolta|sigma|samsung|ricoh|apple|dji|google|flir|casio|sanyo|kyoceraraw|reconyx|hp|jvc|ge|motorola|nintendo|microsoft]=...`
summaries when matching decoded fields are present.
Live-vendor source-processing coverage currently includes Apple computational
capture/HDR/motion fields, DJI pose and thermal fields, Google HDR+/shot-log
fields, and FLIR radiometric/raw-value/geometry fields. These buckets are used
by rendered-image safety filtering; they are not target-owned metadata for
rendered outputs.

## 11. Author And Serialize Metadata

Use the transactional generic builder instead of manually appending unchecked
entries when the application knows exact metadata keys and values.

```cpp
#include "openmeta/metadata_authoring.h"

const std::array entries = {
    openmeta::MetadataAuthoringEntry {
        openmeta::make_exif_tag_key_view("ifd0", 0x010F),
        openmeta::make_value_view_text(
            "Vendor", openmeta::TextEncoding::Ascii),
    },
};
openmeta::MetaStore store;
const auto authored = openmeta::create_metadata_store(entries, &store);
```

The builder deep-copies borrowed values, applies resource limits, finalizes a
temporary store, and calls detached structural/schema validation before
replacing the output. Optional image context lets a codec provide dimensions,
samples per pixel, and color-plane facts it actually owns.

When a host owns unfamiliar container framing, call
`serialize_exif_tiff()` to measure and write unwrapped TIFF/EXIF bytes. Retain
that immutable payload for repeated replay. Do not select a fake
`TransferTargetFormat`; container-specific box, chunk, or marker framing stays
host-owned. See [generic_authoring.md](generic_authoring.md) and
[canonical_serialization.md](canonical_serialization.md).

## Related Docs

- [quick_start.md](quick_start.md)
- [metadata_support.md](metadata_support.md)
- [metadata_transfer_plan.md](metadata_transfer_plan.md)
- [generic_authoring.md](generic_authoring.md)
- [canonical_serialization.md](canonical_serialization.md)
- [development.md](development.md)
