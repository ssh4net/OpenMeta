# Scoped semantic metadata queries

Version 0.8.30 adds image membership and explicit numeric completeness to the
experimental C++ and Python query, interpretation, concept and DNG CCM
services. The caller
selects metadata directories that describe an image. OpenMeta does not choose a
RAW sensor plane or apply metadata to pixels.

## Directory membership

`MetadataQueryOptions::scope` uses `MetadataDirectorySelector { block, ifd }`.
A selector matches the exact original `Entry.origin.block` and IFD token. An
empty token selects every directory in that block. `kInvalidBlockId` selects
unassigned entries; it is not a wildcard. A disabled scope selects the whole
store; an enabled scope with no selectors selects nothing. The selector span
and its strings need only remain valid during the call. Entry IDs in results
refer to the original store.

```cpp
const std::array<openmeta::MetadataDirectorySelector, 2> members {{
    { main_block, "ifd0" }, { raw_block, "subifd0" }
}};
openmeta::MetadataQueryOptions options;
options.scope.enabled = true;
options.scope.directories = members;
options.scope.inherit_across_directories = true;
const auto result = openmeta::query_raw_processing_metadata(store, options);
```

An enabled scope denotes one caller image for concept conflicts and preferences.
The inheritance flag controls cross-directory companions and grouping within
that image; it does not split the selection into multiple conflict contexts.
Use separate calls or the disabled whole-store scope for independent images.

Selection happens before lookup, grouping, suppression and conflict decisions.
Default groups stay within one exact block/directory pair, even if other blocks
reuse its IFD name. Set `inherit_across_directories` only when the selected
members belong to one caller-defined image. Related vendor groups and companion
lookups may then use those members. Complete DNG groups in different directories
remain separate candidates. Whole-store concept preferences and conflicts stay
within each assigned block/directory context; unassigned entries have one
unknown imported context. Several independent contexts may each have a preferred
candidate, without one global `preferred_entry`.

`interpret_metadata`, `interpret_metadata_query`, and the concept resolvers take
the same options. `CcmQueryOptions::scope` uses the same membership rules for
DNG markers, field extraction and companion validation. CCM fields and issues
carry their source block, and fields also carry their original entry ID.

### Python

Python exposes `MetadataQueryScope`, `MetadataDirectorySelector` and
`MetadataQueryOptions`. `Document` and `TransferSourceSnapshot` query,
interpretation, concept and transfer-diagnostic methods accept an optional
`options=` argument. Existing calls without options keep the whole-store
defaults. `Document.dng_ccm_fields` accepts `scope=` and keeps its existing
field-limit arguments.

Selectors own their `ifd` strings. Add selectors with `add_directory` or
assign a list of selector objects to `directories`:

```python
scope = openmeta.MetadataQueryScope()
scope.enabled = True
scope.add_directory(block, "ifd0")
scope.inherit_across_directories = True  # Only for one caller-defined image.
# Equivalent list assignment; the bound vector is copied into the scope.
# scope.directories = [openmeta.MetadataDirectorySelector(block, "ifd0")]

options = openmeta.MetadataQueryOptions()
options.scope = scope
options.max_total_values = 4096
query = document.metadata_query(
    openmeta.MetadataQueryKind.RawProcessing, options=options
)
concepts = document.resolve_metadata_concepts(options=options)
snapshot = document.build_transfer_source_snapshot()
interpretation = snapshot.interpret_metadata(options=options)
ccm = document.dng_ccm_fields(scope=scope)
```

Reading `directories` returns a Python list copy, and assigning a list copies
its elements. Changing or deleting the source list or scope does not invalidate
another scope or options object. An enabled scope with an empty `directories`
list selects nothing. A disabled scope selects the whole store. Numeric limits
set to zero are unlimited; `max_semantic_work = 0` selects the finite default of
1048576 operations.

## Numeric completeness and limits

`numeric_status` distinguishes `NotNumeric`, `Complete`, `Partial` and `Invalid`.
Candidates and records retain `total_numeric_values`; each component retains its
source entry, full count, returned count and offset into the returned vector.
Result totals report returned values and whether a limit or invalid source was
encountered. A missing or empty numeric vector does not imply an empty source.
Partial/invalid concept candidates do not become preferred numeric answers.

The defaults allow 65536 values per source entry, 262144 per candidate and
1048576 per result. Zero disables the respective numeric cap. Set
`include_numeric_values=false` to request counts and components without copying
numeric vectors. Nonempty numeric sources then report `Partial`. Interpretation
and concept resolution share one aggregate value budget across their query
kinds. Malformed array backing and invalid numeric values report `Invalid`;
normalization does not treat a valid prefix as a complete source. Source typed
entries remain accessible and unchanged.

CCM keeps its own field limits and reports `total_values`/`values_complete`.
A limited matrix retains its full shape; validation does not mistake the
returned prefix for the complete matrix.

## Structured roles and coordinates

Components identify CFA dimensions/pattern/plane colors, black-level repeat
positions and horizontal/vertical deltas, white level, WB vector roles, and
matrix subtypes. Dimensions come from declared metadata, not guessed camera
layouts. ColorMatrix is N by 3, CameraCalibration is N by N, and ForwardMatrix
and ReductionMatrix are 3 by N. Calibration index and associated illuminant
entry identify the relevant DNG matrix context.

DefaultCropOrigin/Size geometry uses `ActiveAreaRelativeRaw`; ActiveArea and
MaskedAreas use `StoredImageRelativeRaw`. `Unknown` means the query does not
establish a coordinate basis. These records preserve source relationships; they
do not perform active-area offset addition, scaling, orientation, channel
reordering or pixel reconstruction.

The DNG shapes and coordinate definitions follow the
[Adobe DNG 1.7.1 specification](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf).
All results own their strings/vectors. Objects remain host-synchronized; this
API adds no shared-object concurrency guarantee.


Geometry component vectors retain the source component order. Use the separate
normalized rect/origin/size fields for geometry; an ActiveArea source vector
contains top/left/bottom/right, while its normalized rect contains x/y/width/height.
The coordinate-basis enum identifies the reference frame for that geometry.


Concept companion/composite scans and preference/conflict pair comparisons
share max_semantic_work across all ten kinds in an aggregate call. Its default
is 1048576 operations; zero restores that finite default. Results report
semantic_work_used and semantic_work_limited. If work is exhausted, known
candidates remain available, but affected resolutions clear preferred answers;
conflict=false then does not establish that no conflict exists. Increase the cap
explicitly for a trusted large workload. This counter covers the documented
scans/comparisons rather than every operation in query or concept processing.


Transfer concept preflight accepts the same options through
transfer_concept_diagnostics_from_store, including its RAW-descriptor overload.
Its result exposes semantic-work and numeric-limit/invalid flags so a partial
conflict scan cannot appear complete in host diagnostics. Use an explicit image
scope when related EXIF/XMP/IPTC directories should share one preflight context.
