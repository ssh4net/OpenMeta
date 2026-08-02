# Quick Start

This guide is for the normal OpenMeta use case: a C++ application that needs
to read, query, prepare, and transfer image metadata.

OpenMeta is a metadata engine, not an image encoder. In practice that means:

- read metadata from an existing file
- query it through `MetaStore`
- build new metadata entries when needed
- inject metadata into an existing target file or template
- prepare metadata artifacts for a host API such as EXR or another
  host-owned metadata layer

If you need the full support matrix, see
[metadata_support.md](metadata_support.md). If you need the detailed target
contract, see [metadata_transfer_plan.md](metadata_transfer_plan.md).

If you already own the encoder or host API, see
[host_integration.md](host_integration.md).
For public API adoption status, see [api_stability.md](api_stability.md).
For the stable flat host naming contract, see
[flat_host_mapping.md](flat_host_mapping.md).
For deterministic host compatibility baselines, see
[compatibility_dump.md](compatibility_dump.md).
For generated XMP merge and writeback precedence, see
[xmp_sync_policy.md](xmp_sync_policy.md).
For per-target writer preserve/replace guarantees, see
[writer_target_contract.md](writer_target_contract.md).

## 1. Add OpenMeta To Your CMake Project

If OpenMeta is installed as a package:

```cmake
find_package(OpenMeta CONFIG REQUIRED)

add_executable(my_app main.cc)
target_link_libraries(my_app PRIVATE OpenMeta::openmeta)
```

If you build OpenMeta from source in the same workspace, any normal
`add_subdirectory(...)` or install-and-find-package workflow is fine. The
public target alias is `OpenMeta::openmeta`.

## 2. Build OpenMeta

Library and tools:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Tests:

```bash
cmake -S . -B build-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENMETA_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests
```

If your optional dependencies live in a custom prefix, pass that prefix
through `CMAKE_PREFIX_PATH`.

Optional Adobe DNG SDK bridge:

```bash
cmake -S . -B build-dng -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOPENMETA_WITH_DNG_SDK_ADAPTER=ON \
  -DOPENMETA_USE_LIBCXX=ON \
  -DCMAKE_PREFIX_PATH='<dng-sdk-prefix>;<deps-prefix>'
cmake --build build-dng
```

## 3. Read One File Into `MetaStore`

This is the basic C++ read path.

```cpp
#include "openmeta/simple_meta.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

std::vector<std::byte> file_bytes = load_file_somehow("file.jpg");

openmeta::MetaStore store;
std::array<openmeta::ContainerBlockRef, 256> blocks {};
std::array<openmeta::ExifIfdRef, 512> ifds {};
std::vector<std::byte> payload(1 << 20);
std::array<uint32_t, 1024> payload_indices {};

openmeta::SimpleMetaDecodeOptions options;
const openmeta::SimpleMetaResult read = openmeta::simple_meta_read(
    std::span<const std::byte>(file_bytes.data(), file_bytes.size()),
    store,
    blocks,
    ifds,
    payload,
    payload_indices,
    options);

store.finalize();
```

Notes:

- `simple_meta_read(...)` appends decoded entries to `MetaStore`.
- Call `store.finalize()` before exact-key lookups.
- The caller owns the scratch buffers. That is intentional.

## 4. Query Metadata

Use exact-key lookup through `MetaStore::find_all(...)` when you already know
the metadata family and key:

```cpp
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"

openmeta::MetaKeyView make_key;
make_key.kind = openmeta::MetaKeyKind::ExifTag;
make_key.data.exif_tag.ifd = "ifd0";
make_key.data.exif_tag.tag = 0x010F;  // Make

for (openmeta::EntryId id : store.find_all(make_key)) {
    const openmeta::Entry& entry = store.entry(id);
    // Inspect entry.value and entry.origin here.
}
```

Use exact keys for:

- EXIF tags
- XMP properties
- EXR attributes
- ICC tags
- IPTC datasets

For semantic inspection UI, use `openmeta/metadata_query.h`. It reports raw
matches plus normalized candidates for areas such as crop/active-area,
exposure/gain, white balance, color/profile, lens correction, orientation, and
RAW-processing metadata. Color-profile matches include EXIF color-space
evidence, ICC header/tag entries, XMP ICC/profile/color-space fields, and PNG
profile text carriers. Source-bound camera RAW profile, look, tone-curve, and
style metadata is reported separately as `source_color_transform`, while RAW
value curves, linearity limits, calibration curves, and curve control points
use dedicated RAW-processing semantic roles. Hosts can inspect these values
without treating them as portable rendered-image color or profile metadata.
`query_descriptive_metadata(...)` also exposes a
bounded EXIF/IPTC/XMP reconciliation view for common descriptive fields:
title/headline, description/caption, creator/author, keywords/subject, and
IPTC created/digital-creation date-time evidence.
These helpers use deterministic built-in name, namespace, and tag matching.
Every raw query match retains `exact_match`, `fuzzy_match`, and `fuzzy_score`
compatibility fields, but Semantic Query does not use partial matching because
near names must not change metadata classification.

For vendor MakerNote/RAW fields, the query layer also builds conservative
per-family grouped candidates for related white-balance,
source-color-transform, raw-storage, sensor, computational, thermal,
stitch/panorama, source-processing, raw value curve, linearity-limit,
calibration-curve, and curve-control-point records. These records are for
inspection and safe-transfer policy, not for writing source RAW transforms into
rendered outputs.
Grouped color, white-balance, and lens-correction records require numeric
payloads with conservative minimum shapes before they become matrix/vector/table
candidates. Malformed or text-only source records still remain visible as
per-entry metadata, but they are not promoted into normalized grouped records.
Known geometry patterns can also become normalized rectangles; current public
coverage includes DNG crop/active-area tags, Phase One/Leaf RAW geometry, and
Fujifilm RAF raw crop/zoom fields, plus Canon, Nikon Capture, and Sony
panorama crop/border patterns.

For free-text metadata-name search, use the separate bounded API:

```cpp
#include "openmeta/metadata_fuzzy_search.h"

openmeta::MetadataFuzzySearchOptions options;
options.minimum_score = 80;
options.max_results = 12;

const openmeta::MetadataFuzzySearchResult search =
    openmeta::fuzzy_search_metadata(store, "shuter speed", options);
if (search.status == openmeta::MetadataFuzzySearchStatus::Ok) {
    for (const openmeta::MetadataFuzzySearchMatch& match : search.matches) {
        // match.entry_id, match.name, match.score, and match.match_kind
    }
}
```

Ranking is deterministic: score, then exact/alias/fuzzy provenance, then stable
entry id. The current contract accepts ASCII query text only and returns an
explicit status for non-ASCII input; it does not perform Unicode normalization
or transliteration. Python exposes the same result through
`Document.fuzzy_search(...)` and `TransferSourceSnapshot.fuzzy_search(...)`.
See [fuzzy_search.md](fuzzy_search.md) for alias behavior, resource/threading
bounds, quality gates, benchmark usage, and the large-store indexing decision.

For structured interpretation output, use `openmeta/metadata_interpretation.h`.
It projects semantic query candidates into records that carry the query class,
semantic kind, normalized shape, confidence, source entry ids, and normalized
rect/margin/value arrays where available.

For duplicated concepts that may appear in multiple metadata families, use
`openmeta/metadata_concepts.h`. The first experimental resolver covers
orientation, date/time, exposure/gain, color/profile, GPS, geometry, lens
correction, and RAW-processing. It returns candidate source entries, source
families, preferred entries, normalized date/time fields where available,
date/time precision, timezone kind, GPS altitude-reference state, canonical
geometry origin/size/rect/margins, normalized exposure values, full normalized
value vectors for grouped matrix/vector/table concepts, transfer hints,
compatible/rendered safety booleans, and same-role conflict flags so host UI
can show ambiguity instead of guessing silently. Transfer hints use `safe`,
`source_bound`,
`rendered_unsafe`, or `requires_target_image_spec` to separate portable facts
from source RAW/correction data and target-owned image facts.
`transfer_concept_diagnostics_from_store(...)` applies those hints to a
selected transfer safety mode and returns keep/drop/requires-target-image-spec
actions, severity tokens, stable summary/message tokens, localizable argument
tokens, and default message text for transfer-preview UI.
When a host knows the RAW storage context, pass `MetadataRawDataDescriptor` to
the descriptor-aware overload so RAW curve/linearity and black/white-level
diagnostics can distinguish stored RAW data from rendered pixels.
If a decoder knows that a curve/LUT-like metadata entry is meaningful only for
compressed RAW samples, set
`raw_descriptor.requires_compressed_raw_encoding = true` before requesting
concept diagnostics.
If that curve/LUT only applies to the primary raw plane, also set
`raw_descriptor.requires_primary_raw_plane = true` and provide
`has_plane_index` / `plane_index` for the raw buffer being checked.
Rendered-transfer drop messages distinguish source color transforms, white
balance, lens correction, source RAW curves/linearity metadata, source-bound
RAW processing, and target-owned image properties. Source color transforms
cover camera RAW profiles, looks, tone
curves, and vendor style/rendering tables that should not be treated as
portable rendered-image color profiles. Source-bound RAW processing diagnostics
also keep RAW value curves, linearity limits, calibration curves, curve control
points, computational, thermal, and stitch/panorama roles separate for UI
policy messages. GPS altitude reference codes can be
displayed with
`metadata_concept_gps_altitude_reference_name(...)`.

For user-facing orientation display, use `openmeta/orientation.h` instead of
showing only the numeric EXIF/TIFF index. `interpret_exif_orientation(...)`
returns the index, human-readable label, clockwise rotation degrees, mirrored
state, width/height-swap flag, and nearest rotation-only orientation.

For common enum-like TIFF/EXIF/DNG numeric values, and selected bounded
Canon/Nikon/Sony/Fujifilm/Pentax/Olympus/Panasonic/Phase One/Kodak/Minolta/
Sigma/Samsung/Ricoh MakerNote contexts, use
`openmeta/exif_value_names.h`. Unknown values return an empty string and remain
available as numeric `MetaStore` values.

## 5. Create Fresh Metadata

Use the bounded high-level Creation API when the host has logical values rather
than wire tag IDs:

```cpp
#include "openmeta/metadata_creation.h"

#include <array>

const std::array fields = {
    openmeta::make_metadata_creation_text(
        openmeta::MetadataCreationFieldKind::Title, "Evening frame"),
    openmeta::make_metadata_creation_text(
        openmeta::MetadataCreationFieldKind::Creator, "Alice"),
    openmeta::make_metadata_creation_u32(
        openmeta::MetadataCreationFieldKind::Orientation, 6),
};

openmeta::MetadataCreationRequest request;
request.fields = fields;

openmeta::MetaStore store;
const openmeta::MetadataCreationResult created =
    openmeta::create_metadata(request, &store);
```

The result is a finalized store of canonical portable-XMP entries. The call is
transactional: validation failures leave `store` unchanged. See
[creation.md](creation.md) for field mappings, value constraints, Python use,
and destination-image safety requirements.

### Edit Logical Metadata

Use the matching transactional Editing API when a finalized store already
exists:

```cpp
#include "openmeta/metadata_editing.h"

const std::array operations = {
    openmeta::make_metadata_edit_set(
        openmeta::make_metadata_creation_text(
            openmeta::MetadataCreationFieldKind::Title, "Edited title")),
    openmeta::make_metadata_edit_add(
        openmeta::make_metadata_creation_text(
            openmeta::MetadataCreationFieldKind::Keyword, "approved")),
};

openmeta::MetadataEditingRequest edit_request;
edit_request.operations = operations;

openmeta::MetaStore edited;
const openmeta::MetadataEditingResult edit_result =
    openmeta::edit_metadata(store, edit_request, &edited);
```

`store` is not mutated. The output is replaced only if every operation
succeeds. Set operations preserve entry provenance, while removal creates a
dirty tombstone. See [editing.md](editing.md) for occurrence and conflict
semantics.

Use the lower-level key/value API only when you need a wire-specific or custom
entry.

This is useful when your application creates or edits metadata directly.

```cpp
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"

openmeta::MetaStore store;

const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});

openmeta::Entry make_entry;
make_entry.key = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x010F);
make_entry.value = openmeta::make_text(
    store.arena(), "Vendor", openmeta::TextEncoding::Ascii);
make_entry.origin.block = block;
make_entry.origin.order_in_block = 0;

store.add_entry(make_entry);
store.finalize();
```

The key/value helpers live in:

- [metadata_creation.h](../src/include/openmeta/metadata_creation.h)
- [metadata_editing.h](../src/include/openmeta/metadata_editing.h)
- [meta_key.h](../src/include/openmeta/meta_key.h)
- [meta_value.h](../src/include/openmeta/meta_value.h)

## 6. Export XMP From `MetaStore`

For portable sidecar output:

```cpp
#include "openmeta/xmp_dump.h"

std::vector<std::byte> xmp_bytes;
openmeta::XmpSidecarRequest request;
request.format = openmeta::XmpSidecarFormat::Portable;
request.include_exif = true;
request.include_iptc = true;

const openmeta::XmpDumpResult dump =
    openmeta::dump_xmp_sidecar(store, &xmp_bytes, request);
```

For lossless OpenMeta-native sidecars, switch `request.format` to
`XmpSidecarFormat::Lossless`.

## 7. Copy Metadata Into An Existing Target File

This is the common export workflow:

- one source file provides metadata
- one target file or template already owns the pixel container
- OpenMeta edits the target metadata

```cpp
#include "openmeta/metadata_transfer.h"

openmeta::ExecutePreparedTransferFileOptions options;
options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
options.prepare.prepare.profile.safety =
    openmeta::TransferSafetyMode::RenderedImage;
options.edit_target_path = "rendered.jpg";

const openmeta::ExecutePreparedTransferFileResult exec =
    openmeta::execute_prepared_transfer_file("source.jpg", options);

openmeta::PersistPreparedTransferFileOptions persist;
persist.output_path = "rendered_with_meta.jpg";
persist.overwrite_output = true;

const openmeta::PersistPreparedTransferFileResult saved =
    openmeta::persist_prepared_transfer_file_result(exec, persist);
```

Use the same pattern for other file-backed targets:

- `TransferTargetFormat::Tiff`
- `TransferTargetFormat::Dng`
- `TransferTargetFormat::Png`
- `TransferTargetFormat::Webp`
- `TransferTargetFormat::Jp2`
- `TransferTargetFormat::Jxl`
- bounded BMFF targets such as `Heif`, `Avif`, and `Cr3`

### Read Once And Reuse Later

If your application already decoded source metadata earlier, keep a decoded
source snapshot and execute the later save without reopening the source file:

```cpp
#include "openmeta/metadata_transfer.h"

const openmeta::ReadTransferSourceSnapshotFileResult snapshot =
    openmeta::read_transfer_source_snapshot_file("source.jpg");

openmeta::ExecutePreparedTransferSnapshotOptions options;
options.prepare.target_format = openmeta::TransferTargetFormat::Tiff;
options.edit_target_path      = "target.tif";
options.execute.edit_apply    = true;

const openmeta::ExecutePreparedTransferFileResult result =
    openmeta::execute_prepared_transfer_snapshot(
        snapshot.snapshot, options);
```

Python exposes the same reusable snapshot flow for host code:

```python
from pathlib import Path

import openmeta

snapshot_info = openmeta.read_transfer_source_snapshot_file("source.jpg")
snapshot = snapshot_info["snapshot"]

result = openmeta.transfer_snapshot_probe(
    snapshot,
    target_format=openmeta.TransferTargetFormat.Tiff,
    edit_target_path="target.tif",
    target_bytes=Path("target.tif").read_bytes(),
)
```

By default, source snapshots are decoded-store-backed. They are meant for the
common EXIF/XMP/ICC/IPTC transfer workflow, where OpenMeta re-emits decoded
metadata after applying the selected safety policy.
If a host needs provenance for a later passthrough policy decision, enable
`ReadTransferSourceSnapshotFileOptions::preserve_raw_carriers` or pass
`preserve_raw_carriers=True` in Python. The snapshot then keeps bounded raw
carrier records with original `ContainerBlockRef` ranges, route hints, and
payload bytes up to `max_raw_carrier_bytes`. Each carrier also reports the
snapshot-local decoded entry ids attributed to it. Transfer still uses decoded
re-emission by default.
Use `raw_carrier_passthrough_audit_from_snapshot(...)` to preflight which
preserved carriers are candidates for a target format and which are blocked by
missing payloads, target incompatibility, active safety filtering,
content-bound C2PA, explicit profile policy, missing decoded entry links, or an
unsupported carrier kind.
Python exposes the same check as
`snapshot.raw_carrier_passthrough_audit(...)`.
To allow the bounded passthrough path during snapshot preparation, set
`PrepareTransferRequest::raw_carrier_passthrough_mode` to
`TransferRawCarrierPassthroughMode::WhenSafe`, or pass
`raw_carrier_passthrough_mode=openmeta.TransferRawCarrierPassthroughMode.WhenSafe`
to Python snapshot transfer helpers. The current passthrough emitter is
deliberately narrow: it can reuse eligible non-C2PA JUMBF and OpenMeta draft
unsigned C2PA invalidation carriers for JPEG, JXL, and BMFF targets, plus
draft unsigned C2PA invalidation carriers for WebP. EXIF/XMP/ICC/IPTC still
use decoded re-emission.
If the host owns the final file write path, the lower-level
`prepare_metadata_for_target_snapshot(...)` entry point remains available.
If the host already has a decoded `MetaStore`, build a reusable snapshot with
`build_transfer_source_snapshot(store)`. If it already owns the source bytes in
memory, use `read_transfer_source_snapshot_bytes(bytes)` instead of the
file-path reader.
In Python, if the host already has `doc = openmeta.read(...)`, call
`doc.build_transfer_source_snapshot()` or
`openmeta.build_transfer_source_snapshot(doc)` instead of reopening the file.
If it also owns the destination bytes in memory, call the overload
`execute_prepared_transfer_snapshot(snapshot, target_bytes, options)`.
If it already holds a prepared bundle, use
`execute_prepared_transfer_bundle(bundle, target_bytes, options)` instead.
Snapshot execution supports the same existing-sidecar merge and destination
carrier-precedence controls as the file helper; when loading an existing
sidecar it defaults to `edit_target_path` unless
`xmp_existing_sidecar_base_path` is set explicitly.
For embedded-only writeback with sidecar cleanup and no filesystem path, set
`xmp_existing_destination_sidecar_state` explicitly so OpenMeta can return a
cleanup decision without guessing a sidecar location.
The Python snapshot helpers intentionally cover the core transfer/edit/persist
path. The older `transfer_probe(...)` / `unsafe_transfer_probe(...)` entry
points remain the broader artifact-dump/debug surface.

## 8. Check Runtime Capabilities

Use the capability API before enabling format/family operations in a host UI
or integration layer.

```cpp
#include "openmeta/metadata_capabilities.h"

const openmeta::MetadataCapability cap = openmeta::metadata_capability(
    openmeta::TransferTargetFormat::Avif,
    openmeta::MetadataCapabilityFamily::Xmp);

if (openmeta::metadata_capability_available(cap.target_edit)) {
    // The current build can edit AVIF XMP within the reported support level.
}
```

Each operation reports `unsupported`, `supported`, `bounded`, or `disabled`.
`bounded` means the capability exists within OpenMeta's documented contract.
`disabled` is used for compile-time-disabled support such as XMP decode when
XML support is not available.

Python exposes the same query:

```python
cap = openmeta.metadata_capability(
    openmeta.TransferTargetFormat.Avif,
    openmeta.MetadataCapabilityFamily.Xmp,
)
print(cap["target_edit_name"])
```

## 9. Prepare Metadata For Host-Owned Encoders

Some applications do not want a file helper. They already own the encoder or
the output container and just want OpenMeta to prepare metadata bytes.

### EXR Host API

EXR is the cleanest host-adapter example.

```cpp
#include "openmeta/exr_adapter.h"

openmeta::ExrAdapterBatch batch;
openmeta::BuildExrAttributeBatchFileOptions options;

const openmeta::BuildExrAttributeBatchFileResult result =
    openmeta::build_exr_attribute_batch_from_file(
        "source.jpg", &batch, options);

for (const openmeta::ExrAdapterAttribute& attr : batch.attributes) {
    // Forward attr.name, attr.type_name, and attr.value to your EXR writer.
}
```

Use this when the host already writes EXR files through OpenEXR or its own EXR
code.

### Generic Host Metadata Export

If your application owns the metadata object model, use the generic traversal
API and build the mapping in the host layer.

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
options.name_policy        = openmeta::ExportNamePolicy::ExifToolAlias;
options.include_makernotes = true;

MyMetadataSink sink;
openmeta::visit_metadata(store, options, sink);
```

### JPEG / JXL / Other Encoder-Owned Outputs

For host-owned encoders, the transfer core exposes two patterns:

1. implement a backend emitter such as `JpegTransferEmitter` or
   `JxlTransferEmitter`
2. or build a target-neutral adapter view and consume explicit operations

Minimal JPEG/JXL-style pattern:

```cpp
#include "openmeta/metadata_transfer.h"

openmeta::PrepareTransferFileOptions prepare;
prepare.prepare.target_format = openmeta::TransferTargetFormat::Jxl;

openmeta::PrepareTransferFileResult prepared =
    openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

openmeta::PreparedTransferExecutionPlan plan;
openmeta::compile_prepared_transfer_execution(
    prepared.bundle, openmeta::EmitTransferOptions {}, &plan);

// Then either:
// - implement openmeta::JxlTransferEmitter and call
//   emit_prepared_transfer_compiled(...)
// - or build_prepared_transfer_adapter_view(...) and feed the ops into your
//   own backend abstraction
```

OpenMeta does not currently ship a TurboJPEG-specific wrapper, but the JPEG
transfer path is designed for that kind of integration through
`JpegTransferEmitter` and the adapter-view APIs.

For fuller C++ host-side examples, see
[host_integration.md](host_integration.md).

## 10. Optional Adobe DNG SDK Bridge

If OpenMeta was built with `OPENMETA_WITH_DNG_SDK_ADAPTER=ON`, you can update
an existing DNG file through the Adobe SDK bridge:

```cpp
#include "openmeta/dng_sdk_adapter.h"

openmeta::ApplyDngSdkMetadataFileResult result =
    openmeta::update_dng_sdk_file_from_file("source.jpg", "target.dng");
```

This is for applications that already use the Adobe DNG SDK. It is not a raw
image encoder.

## 11. CLI And Python Are Convenience Layers

The CLI and Python bindings are useful, but they are thin layers over the same
public C++ APIs.

Transfer writeback follows the C++ file-helper contract. With `--output`,
generated sidecar paths are derived from the output path, not from the input or
target path. `--xmp-writeback sidecar` suppresses generated embedded XMP and
writes the generated packet as `output-stem.xmp`. `--xmp-writeback
embedded_and_sidecar` writes both carriers. Embedded-only writeback preserves
an existing sidecar unless `--xmp-destination-sidecar strip_existing` is set;
sidecar-only writeback preserves existing embedded XMP unless
`--xmp-destination-embedded strip_existing` is set. `--force` controls
overwriting output files and generated sidecars.

CLI:

```bash
./build/metaread file.jpg
./build/metatransfer --source-meta source.jpg --target-jpeg rendered.jpg --output rendered_with_meta.jpg --force
./build/metatransfer --source-meta source.jpg --target-jpeg rendered.jpg --xmp-writeback embedded_and_sidecar --output rendered_with_meta.jpg --force
./build/metatransfer --source-meta source.jpg --target-jpeg rendered.jpg --xmp-destination-sidecar strip_existing --output rendered_with_meta.jpg --force
```

Python:

```bash
PYTHONPATH=build-py/python python3 - <<'PY'
import openmeta

doc = openmeta.read("file.jpg")
print(doc.entry_count)
PY

PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer \
  --source-meta source.jpg \
  --target-jpeg rendered.jpg \
  --xmp-writeback embedded_and_sidecar \
  --output rendered_with_meta.jpg \
  --force
```

## 12. Pick The Next Doc

- Detailed read support:
  [metadata_support.md](metadata_support.md)
- Target-by-target transfer status:
  [metadata_transfer_plan.md](metadata_transfer_plan.md)
- Host-side encoder and SDK integration:
  [host_integration.md](host_integration.md)
- Build, test, and deeper API notes:
  [development.md](development.md)
