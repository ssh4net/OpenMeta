# Migrating to OpenMeta 0.8

OpenMeta 0.8.0 retains the **unfrozen development ABI label 4**. The public
`PreparedTransferBundle` layout changes: rebuild applications and plugins against
matching headers and libraries. Request `find_package(OpenMeta 0.8 CONFIG REQUIRED)`.
The installed `SameMajorVersion` policy accepts older 0.x minimum requests,
such as `find_package(OpenMeta 0.7 CONFIG REQUIRED)`. Higher minimums and
mismatched `EXACT` requests are rejected; version ranges retain their bounds.
Package selection does not guarantee unchanged experimental APIs or binary
compatibility. ABI label 4 alone does not establish compatibility with older
binaries.

## Bounded metadata work

Patch 0.8.2 adds `JumbfDecodeLimits::max_semantic_work`. It bounds aggregate
JUMBF/C2PA box, projection, reference, link, comparison and sorting work that
can otherwise multiply independently bounded collection sizes. Zero selects the
finite default; it does not disable this limit. Exhaustion returns
`JumbfDecodeStatus::LimitExceeded`. Increase the value explicitly only for a
trusted workload that needs more semantic projection work.

The zlib and Brotli payload paths now enforce `PayloadLimits::max_output_bytes`
before each decoder call. An output that ends exactly at the limit still
succeeds. Output that requires another byte returns
`PayloadStatus::LimitExceeded` without writing beyond the configured limit.
Zero retains its documented unlimited meaning for this payload limit.

`JumbfDecodeLimits` is an experimental public layout. OpenMeta keeps development
ABI label 4 for this patch, but applications and plugins must rebuild against
matching 0.8.2 headers and libraries.

## Bounded BMFF edits

Patch 0.8.1 preserves ordered repeated `dimg` inputs during bounded BMFF edits.
Removal of an input of a retained derived-image relation now rejects before
output instead of silently shortening the list. Removing the source still drops
its whole relation. This patch changes no public signatures or layouts.

## GPS edit ownership

The primary, navigation, destination, quality and text GPS translation contracts
are version 2. Their signatures, options, accepted formats, precision limits and
version rules stay the same. Together they cover the 31 native GPS value tags and
the structural `GPSVersionID` tag.

Each accepted source group owns its native members: coordinates own reference
and DMS values, altitude owns reference and magnitude, UTC timestamps own time
and date, and navigation/destination pairs own reference and value. Other GPS
fields are independent. Omitted or ineligible sources preserve destination values.
The existing conflict policy applies to the complete source-native group.
PreserveExisting skips an active native group even when it is an exact match;
it does not promote clean values into edit authority.

An accepted dirty deletion creates or reuses native `Dirty|Deleted` intent even
when the native members were absent. Accepted exact clean values gain `Dirty`
while retaining their native fractions, array/text bytes and wire provenance.
All additions, authority updates and duplicate removals consume the existing
preflight budgets. Repeated calls reuse intent; failure leaves source and output
unchanged, including aliased output.

Each translator is transactional. Hosts stage the five calls before publishing
an aggregate result and synchronize access to shared objects.

## Prepared TIFF and BigTIFF edits

`PreparedTransferBundle` adds `tiff_gps_removals`, a sorted unique list of native
GPS IDs 1 through 31, and `tiff_merge_existing_gps`, which defaults to false.
With EXIF output enabled, dirty supported native GPS edits select bounded merge
mode. A live native member wins over a tombstone for the same key. Tag 0 alone
never selects this mode. Source snapshot v1 retains the flags and regenerates
these fields without changing its format. Python snapshot probes expose both.

Bounded editing preserves unselected destination GPS fields, including unknown
tags. Removing selected GPS fields does not clear the directory. The final GPS
version and pointer are removed only when no non-version GPS value remains.
An explicit empty GPS payload still means whole-directory clearing; combining
it with bounded merge/removals or conflicting manual pointer edits is rejected.

An active bounded merge requires one valid supplied native GPSVersionID.
Supplied and existing destination versions must agree. Conflicts fail before
output; no automatic version upgrade or altitude reference reinterpretation
occurs. Include the target native GPSVersionID in the translation input when
editing a version other than the default 2.3.0.0. Removal-only edits retain the
destination version while other GPS values survive. Existing exact rational,
encoded-text and legacy sea-level altitude contracts continue to apply.

TIFF/BigTIFF edit, stream and package paths carry this intent. Fresh creation
can consume merge-only metadata because it has no destination values to preserve.
Consumers whose schemas cannot represent removal reject it before writing.
The DNG SDK adapter also rejects GPS merge mode because it cannot preserve
unselected destination GPS fields under this contract. These changes do not add
private-record offset repair or codec conformance guarantees.


Spatial destination editing in 0.8.4
----------------------------------

Spatial translation contract 2 adds Dirty authority for accepted exact native
values and complete native deletion intent for absent keys. Focal-plane X/Y/unit
remain one group; SubjectArea and SubjectLocation remain independent arrays.
Existing parsing and coordinate semantics are unchanged. PreserveExisting keeps
an active owner whole. New intent and promotions consume existing bounds.

The existing native ExifIFD merge/removal fields now cover all five spatial tags
(44 allowlisted tags total) for TIFF/BigTIFF/DNG destination edits. Prepared bundle
layout, signatures, options and source snapshot version 1 remain unchanged.
Non-edit consumers reject removal intent. Rebuild against matching headers and
libraries: development ABI 4 is unfrozen. SameMajorVersion package selection
continues to allow later source versions; it does not promise experimental API
or binary compatibility. OIIO/iRAW adoption remains a separate acceptance gate.


Rational and identity destination editing in 0.8.5
--------------------------------------------------

Rational and identity translation contracts are version 2. SubjectDistance,
DigitalZoomRatio, ExposureIndex and FlashEnergy are four independent scalar
owners. LensSpecification owns its four-element array; ImageUniqueID is independent.
Accepted exact clean natives gain Dirty while retaining rational components,
lens unknown markers, ID case/NUL and wire provenance. Complete eligible deletion
carries native intent even for absent keys. Omission and ineligible sources
preserve destination values; PreserveExisting retains an active owner without
promotion. Existing budgets include intent, promotion and duplicate-removal costs.

The shared ExifIFD merge/removal allowlist grows from 44 to 50 using existing
prepared fields. Dirty selects bounded merge; ordinary prepared EXIF may include
clean live source entries too. Destination entries absent from the prepared
payload are preserved. Native wire values, SubjectDistance numerator sentinels,
lens unknown aperture rules and 32-hex identity semantics are unchanged.

Public signatures, options, bundle layout and snapshot v1 remain unchanged.
Development ABI 4 is unfrozen; rebuild consumers against matching headers and
libraries. SameMajorVersion package selection is unchanged. Hosts stage separate
translator calls before publishing and synchronize shared objects. These changes
do not add reader, codec, SDK, private-offset repair or downstream qualification.

Native ImageUniqueID text may retain its single terminal NUL. Detached validation
and serialization both use the actual 33-byte wire representation, rather than
counting or emitting another terminator. Invalid hex, embedded/multiple NULs and
incorrect wire-count hints remain invalid.
