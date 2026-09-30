# Metadata Editing

`openmeta/metadata_editing.h` provides a bounded transactional editing contract
for the same logical fields accepted by the high-level Creation API. It edits
canonical portable-XMP entries in a finalized `MetaStore` without requiring a
host to work with namespace paths or entry IDs.

The API is experimental and versioned by
`kMetadataEditingContractVersion == 1`.

## C++ Example

```cpp
#include "openmeta/metadata_editing.h"

#include <array>

const std::array operations = {
    openmeta::make_metadata_edit_set(
        openmeta::make_metadata_creation_text(
            openmeta::MetadataCreationFieldKind::Title, "Edited title")),
    openmeta::make_metadata_edit_add(
        openmeta::make_metadata_creation_text(
            openmeta::MetadataCreationFieldKind::Keyword, "approved")),
    openmeta::make_metadata_edit_remove(
        openmeta::MetadataCreationFieldKind::Creator, 0),
};

openmeta::MetadataEditingRequest request;
request.operations = operations;

openmeta::MetaStore edited;
const openmeta::MetadataEditingResult result =
    openmeta::edit_metadata(source, request, &edited);
```

`source` must be finalized. The output is replaced only after every operation
has passed validation and the entire edit has committed. On failure, `edited`
is unchanged and `failed_operation_index` identifies the rejected operation
when available.

## Operation Semantics

| Operation | Behavior |
| --- | --- |
| `Add` | Creates an absent singleton or appends a creator/keyword value. Adding an existing singleton is an explicit conflict. |
| `Set` | Replaces one active value. Existing key, origin, block, wire provenance, and flags are preserved; `Dirty` is added. |
| `Remove` | Marks one active value as `Deleted | Dirty`. The tombstone remains available for writeback and audit behavior. |
| `RemoveAll` | Tombstones every active occurrence. This can repair malformed duplicate singleton fields before a new value is added in the same transaction. |

Operations observe earlier operations in request order. For repeated creators
and keywords, `occurrence` is a zero-based index into the current active
logical values. Removing an occurrence shifts later values for subsequent
operations. New repeated values receive the next unused canonical property
index; existing index gaps are not renumbered.

Singleton fields accept only occurrence zero. If a malformed store contains
multiple active copies, single-value `Set` and `Remove` return
`AmbiguousTarget`; use `RemoveAll` followed by `Add` when that repair is
intended. Missing targets are errors rather than silent no-ops.

## Provenance And Blocks

`Set` changes only the value and dirty flag. `Remove` changes only entry flags.
Both therefore retain the original block and wire provenance.

`Add` emits a new dirty canonical portable-XMP entry. When the store already
contains an XMP entry with a valid block, the new entry uses that block and a
later deterministic order. A finalized empty store can also be edited; its new
entry has no source block because no original carrier exists. Portable XMP and
transfer preparation can serialize that entry normally.

Editing does not compact tombstones automatically. Call the lower-level
`compact(...)` helper only when losing deleted-entry identity is appropriate
for the host workflow.

## Validation And Limits

`Add` and `Set` use exactly the Creation field mapping and value validation.
Text must be non-empty valid UTF-8 and XML 1.0 character data. Orientation,
rating, dimensions, color space, ISO, and rational values use the constraints
documented in [creation.md](creation.md).

The hard maxima are:

- `1024` operations
- `1 MiB` of UTF-8 text per `Add` or `Set` operation
- `8 MiB` of UTF-8 text per request

`MetadataEditingRequest::limits` may lower but not raise these bounds. The
implementation keeps no global state. Concurrent calls are safe when callers
use distinct output stores and do not mutate the finalized source.

## Python

Python operation objects own their text and pass the same request to C++:

```python
import openmeta

K = openmeta.MetadataCreationFieldKind
edited = document.edit_metadata([
    openmeta.metadata_edit_set(
        openmeta.metadata_creation_text(K.Title, "Edited title")),
    openmeta.metadata_edit_add(
        openmeta.metadata_creation_text(K.Keyword, "approved")),
    openmeta.metadata_edit_remove(K.Creator, 0),
])
```

The method returns a detached edited `Document`; the original document is not
mutated. The result can be queried, dumped as XMP, or converted into a transfer
snapshot. Invalid requests raise `ValueError` with the C++ status and rejected
operation index.

## Current Scope

The logical API edits the 24 fields listed in [creation.md](creation.md).
The exact-key API below covers supported typed EXIF/IPTC/XMP/custom entries.
Structural block editing, a full EXIF/IPTC/XMP synchronization engine and direct
in-place file patching remain separate concerns. Supported edited creation dates can
be projected explicitly into native EXIF/IPTC groups before persistence; see
[translation.md](translation.md). Lower-level `MetaEdit` remains available for
entry-ID-based host code, while transfer and writer APIs handle container
persistence.

## Exact typed keys (0.5.6)

`edit_metadata_typed(...)` adds an experimental v1 C++ contract for exact
EXIF/TIFF, IPTC-IIM and XMP keys, including private EXIF and custom XMP. It uses
`MetadataAuthoringEntry` and the borrowed `MetaValueView` helpers from generic
authoring. It copies all supplied keys and payloads. The logical-field API above
continues to provide its own field-specific aliases and repeated-list behavior.

```cpp
#include "openmeta/metadata_editing.h"

openmeta::MetadataTypedEditingOperation operation;
operation.kind = openmeta::MetadataEditingOperationKind::Set;
operation.entry.key = openmeta::make_exif_tag_key_view("exififd", 0xa405);
operation.entry.value = openmeta::make_value_view_u16(50);
const auto result = openmeta::edit_metadata_typed(
    source, std::span(&operation, 1), &source);
```

The finalized base and output may alias. Every supplied Add/Set value and the
complete final candidate must pass structural and selected schema validation
before publication. A failed request leaves both stores unchanged and reports
an operation index or final candidate entry when available. Values overwritten
later in the request must still be valid. A malformed unrelated base entry can
therefore fail final validation; the caller can select schema policy through
`MetadataTypedEditingOptions::validation`.

| Operation | Exact-key behavior |
| --- | --- |
| Add | Default `FailIfPresent` rejects an active exact key. Explicit `Append` permits another occurrence, subject to final schema singleton checks. New entries are dirty and have no original source block. |
| Set | Default `kMetadataTypedEditingUniqueOccurrence` requires exactly one active match. A numeric occurrence selects the zero-based current active match. The value and wire hints are replaced; source entry identity, block and order remain. Omitted wire hints request inference, and obsolete wire type names are cleared. |
| Remove | Uses the same occurrence rules, or `kMetadataEditingAllOccurrences` for all active matches. Tombstones preserve identity and provenance. Missing targets fail. Value and wire hints are ignored. |

Operations observe earlier edits, including additions and shifted occurrences.
Removing all duplicate singleton entries followed by Add can repair a store in
one transaction. XMP path indices remain part of the exact key; deleting
`subject[1]` does not renumber `subject[2]`. This API performs no alias matching,
namespace migration, container restructuring or automatic translation.

Defaults bound the request to 4096 operations, 8 MiB of borrowed key/payload
bytes, 200000 output entries, 64 MiB of output arena/value bytes and 4096 bytes
per key component. Remove-all expansion counts against the operation budget.
Output limits include retained tombstones and arena bytes; editing does not
compact them. Existing lower store ceilings remain in force. Preparation may
allocate. The host must synchronize conflicting access to shared objects;
there are no library atomics or mutexes in this path.

The combined capture fixture now edits typed XMP, translates sixteen API groups,
serializes/restores a transfer snapshot, and checks native and portable values
through JPEG, classic TIFF and BigTIFF add/replace workflows. Exact-key authoring
and editing remain C++ APIs; Python retains logical editing and mirrors the
capture translation calls.

The 0.5.8 structured capture batch adds OECF, SpatialFrequencyResponse,
CFAPattern and DeviceSettingDescription through
`translate_xmp_structured_capture_metadata` and its Python counterpart.
The 0.5.8 combined inventory was 65 distinct native tags across fifteen APIs
(62 retained by compatible-file transfer). ABI 3 and host synchronization
remain unchanged; big-endian snapshots for the four new fields need a 0.5.8
reader. See [Structured capture data](translation.md#structured-capture-data-058).

## EXIF text update (0.5.9)

The 0.5.9 text/version batch adds UserComment, both version fields and seven
EXIF 3 text tags. The new EXIF text API also supports UTF-8 owner/lens fields.
The combined capture inventory is 75 distinct ExifIFD tags across sixteen APIs;
compatible-file transfer retains 72. Version and Artist/Software companion
requirements are explicit. ABI 3, snapshot v1 layout and host synchronization
responsibilities remain unchanged. BOM-less big-endian UserComment snapshots
require a 0.5.9 reader. See the EXIF text/version translation contract for limits
and the recorded OIIO/ExifTool reader limitations.

## EXIF 3.1 development and correction update (0.5.11)

Typed editing and native validation use the same A40D–A412 contracts as
`translate_xmp_development_correction_metadata`: packed DevelopmentType,
published `DevelopmentCharacterstic` spelling, UTF-8 type-129 description and
bounded correction codes. The reverse translator requires an explicit host
EXIF version policy of 0300 or 0310 and commits the complete selected batch
atomically. Transfer retains present values, including rendered-image transfer;
it does not infer RAW or MakerNote processing history. See
[the translation contract](translation.md#exif-31-development-and-correction-data-0511).

## EXIF 3.1 LearningOptOutIn and profile authoring update (0.5.12)

Typed editing and native validation now share the bounded LearningOptOutIn and
profile translation contracts. LearningOptOutIn requires a complete
`exifEX` usage/intention structure and an explicit EXIF 0300/0310 host policy.
Profile writeback covers ImageDescription, Artist, scalar Copyright, ColorSpace
and RelatedSoundFile; it does not infer ICC, image or audio authority or impose
complete-file companions. Failed validation, limits, conflicts and tombstones
remain transactional. See [the translation contract](translation.md#exif-31-learningoptoutin-data-0512).

## Profile editing through file writeback (0.5.14)

The grouped profile lifecycle covers exact typed XMP Add/Set/Remove operations,
`DirtyOnly` profile translation, store validation, portable XMP, serialized
transfer snapshots and JPEG/classic TIFF/BigTIFF writeback. A second JPEG pass
edits the first output and removes Artist with explicit `ReplaceExisting`, checking
that the previous native and XMP values disappear from the existing carrier.
Custom XMP survives when `PreserveCustom` is selected. Invalid values and native
conflicts leave the source and the previously translated result unchanged.

TIFF and BigTIFF merge native IFD0 updates into the destination. Omitting Artist
from a prepared snapshot does not remove an existing destination IFD0 Artist,
even when the new XMP packet omits it. In 0.5.14 the second-pass regression
recorded this boundary: description replacement and XMP removal succeeded,
while the old native Artist remained. In 0.6.0 the same lifecycle carries the
explicit native tombstone and removes Artist; a separate omission-only control
continues to verify destination preservation.

Since 0.6.0, supported native tombstones can request that deletion explicitly;
see the contract below. The omission-only behavior remains unchanged.

Each API call is a separate transaction. The host publishes the completed
sequence only after all required calls succeed. The host also supplies the
destination image ColorSpace during transfer preparation; translating source
ColorSpace does not bypass the existing image-property filtering policy. These
checks do not establish ICC agreement, an audio association or complete-file
profile conformance.

## Explicit native TIFF IFD0 deletion (0.6.0)

Preparation carries dirty native tombstones for IFD0 ImageDescription, Artist
and Copyright in `PreparedTransferBundle::tiff_ifd0_removals`. Both `Dirty` and
`Deleted` are required, the IFD key must be exactly `ifd0`, and EXIF transfer
must be enabled. A live same-key entry wins over historical tombstones. Source
absence alone preserves destination tags.

TIFF/BigTIFF edit plan/apply, stream and edit-package paths consume the explicit
list. A manual list must be sorted, unique and limited to those three tags;
contradictory replacements fail before output. Fresh emitters and codec
handoffs that cannot delete destination entries reject the bundle. This is an
ABI-4 change: rebuild against 0.6.0. See [migration details](migration_0_6.md).

### Technical identity deletion (0.6.2)

The same bounded removal list also accepts IFD0 Make `010F`, Model `0110` and
Software `0131`. Edit `tiff:Make`, `tiff:Model` or `xmp:CreatorTool`, then call
`translate_xmp_technical_metadata` with `DirtyOnly` and `ReplaceExisting` to
produce the native dirty tombstones. Snapshot preparation and TIFF/BigTIFF
edit, stream and package paths retain the requests. An absent source value
preserves the existing native tag. A live native value wins over historical
tombstones; a manually supplied removal conflicting with a replacement fails
before output. The group is tested in both byte orders for classic TIFF and
BigTIFF, with unrelated descriptive values and strip bytes preserved.

In 0.6.2, the six supported tags form a sorted unique list. Its contract
excludes DateTime and its ExifIFD companions, other IFDs/pages, structural tags
and MakerNotes. Version 0.7.0 adds the timestamp contract below. Removing Make
or Model does not remove private camera records;
removing Software does not remove separate software-detail fields. Hosts own
those companion decisions. Fresh emitters and codec handoffs still reject
deletion-bearing bundles. ABI 4 and snapshot v1 are unchanged; these additional
tags require a 0.6.2 or newer library.

### Grouped timestamp lifecycle (0.7.0)

All three EXIF timestamp families now support explicit native deletion and
companion synchronization across primary IFD0 and ExifIFD. Accepted XMP date
owners produce missing-companion removal intents even when the native source
keys are absent. Preparation enables ExifIFD merging for dirty timestamp edits;
unselected timestamp families and unrelated native destination entries survive.
Source omission still preserves values. Manual lists are validated before output
and do not implicitly synchronize XMP. The bundle layout requires matching consumer builds; the current development
ABI label remains 4 and is unfrozen.
See [migration and authority rules](migration_0_7.md).

## Capture and lens editing (0.7.1)

Use logical or exact-key XMP edits, then stage capture, Flash, sensitivity and
camera-text translation before snapshot preparation and TIFF/BigTIFF editing.
Accepted deletions carry native intent even when the source native key was
missing. Clean exact native values gain Dirty authority without changing their
bytes or wire hints. Omission preserves destination values; budgets and conflict
policies are checked before each transaction is published.

Full sensitivity owns its validated seven-tag group, including optional absence;
basic ISO owns only the base tag. Lens make/model/serial are independent fields.
TIFF edits preserve unspecified native records, opaque MakerNote bytes and
original external offsets, standard pointers and media. The selected fields, contract versions and exclusions are defined in
[translation](translation.md#grouped-capture-and-lens-lifecycle-071) and
[migration](migration_0_7.md). The development ABI label stays 4 and remains
unfrozen; build matching headers and libraries.
