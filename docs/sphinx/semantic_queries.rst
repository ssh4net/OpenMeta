Scoped semantic metadata queries
================================

Version 0.8.30 extends the experimental query, interpretation, concept and DNG
CCM services. Rebuild applications against matching headers and binaries;
unfrozen ABI label 4 is retained.

Directory membership
--------------------

``MetadataQueryOptions::scope`` uses exact original block/IFD selectors.
A disabled scope selects the whole store. An enabled empty scope selects
nothing. An empty IFD selects all directories in that exact block, and
``kInvalidBlockId`` selects unassigned entries rather than acting as a wildcard.
The borrowed selector span and strings need only remain valid during the call.
Results retain original EntryIds.

An enabled scope denotes one caller image for concept conflicts/preferences.
The inheritance flag controls companions/groups within that image, without
splitting its conflict context. Use separate calls or whole-store scope for
independent images.

Selection precedes lookup, grouping, suppression and conflict decisions.
Default groups remain within exact block/directory pairs. Enable
``inherit_across_directories`` only for selected members of one caller-defined
image. Related vendor groups and companion lookups can then use those members;
complete DNG groups in different directories remain separate candidates.
Whole-store concept preferences and conflicts stay within assigned
block/directory contexts. Unassigned entries share an unknown imported context.
Multiple independent contexts can have preferred candidates without a global
``preferred_entry``.

The same options apply to interpretation and concept resolution.
``CcmQueryOptions::scope`` uses the same membership for DNG markers, fields and
companion validation. The caller retains responsibility for RAW-plane selection.

Numeric completeness
--------------------

``numeric_status`` is ``NotNumeric``, ``Complete``, ``Partial`` or ``Invalid``.
Candidates/records expose full counts; components identify source entries,
returned ranges, full counts, roles, shapes and illuminant associations.
Partial/invalid numeric concept answers are not preferred. An empty returned
vector does not establish an empty source. Source typed entries are unchanged.

Default limits are 65536 values per entry, 262144 per candidate and 1048576 per
result. Zero disables the respective cap. ``include_numeric_values=false``
retains counts/components without vectors; nonempty sources report Partial.
Interpretation and concept resolution share the aggregate budget across query
kinds. CCM retains its own limits and adds ``total_values``/``values_complete``.
Limited matrices retain their full shape for validation.

Roles and coordinate basis
--------------------------

Components distinguish CFA dimensions/pattern/plane colors, black-level repeat
positions and horizontal/vertical deltas, white level and distinct WB roles.
ColorMatrix is N by 3, CameraCalibration N by N, ForwardMatrix/ReductionMatrix
3 by N. Dimensions use declared metadata, without camera-layout guesses.

DefaultCropOrigin/Size uses ``ActiveAreaRelativeRaw``; ActiveArea and MaskedAreas
use ``StoredImageRelativeRaw``. Other geometry may have an Unknown basis.
These records do not apply active-area offsets, scaling, orientation, channel
reordering or pixel reconstruction. Definitions follow the
`Adobe DNG 1.7.1 specification <https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf>`_.

Results own their strings/vectors. Hosts synchronize shared objects; these APIs
add no shared-object concurrency guarantee.


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
