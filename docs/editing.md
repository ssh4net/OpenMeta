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

This milestone edits the 24 logical fields listed in
[creation.md](creation.md). It does not yet provide high-level arbitrary
EXIF/IPTC/XMP/custom-key operations, language-alternative selection beyond
`x-default`, structural block editing, a full EXIF/IPTC/XMP synchronization
engine, or direct in-place file patching. Supported edited creation dates can
be projected explicitly into native EXIF/IPTC groups before persistence; see
[translation.md](translation.md). Lower-level `MetaEdit` remains available for
entry-ID-based host code, while transfer and writer APIs handle container
persistence.
