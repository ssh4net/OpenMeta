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

## Additional capture and environment editing in 0.8.6

The additional-capture and environment translation contracts are version 2.
The nine fields are FocalLengthIn35mmFilm, FileSource, SceneType, Temperature,
Humidity, Pressure, WaterDepth, Acceleration and CameraElevationAngle. Accepted
exact clean native values gain `Dirty` while retaining raw rational components,
unknown numerators and reserved denominator bits, one-byte UNDEFINED codes,
flags and wire provenance. Complete eligible deletion creates or reuses
`Dirty|Deleted` native intent even when the key is absent. Omitted and ineligible
sources preserve the destination; `PreserveExisting` retains an active owner
without promotion. Existing budgets include promotion, intent and duplicate
removal; failure leaves source and output unchanged.

The shared ExifIFD merge/removal allowlist grows from 50 to 59. `Dirty` selects
merge mode; the prepared EXIF payload may also include clean live source entries.
Unspecified destination entries are preserved when absent from that payload.
Existing signatures, options, prepared bundle layout, snapshot v1 and package
selection policy remain the same. Keep development ABI label 4 and rebuild
consumers against matching headers and libraries. This batch changes no reader,
pixel/RAW transfer exclusion, vendor interpretation or codec contract.


Scoped metadata services in 0.8.30
---------------------------------

Query, interpretation and concept calls now accept MetadataQueryOptions.
CcmQueryOptions gains the same directory scope. Numeric outputs carry full
counts, completeness, component roles/shapes and crop coordinate basis. These
experimental public layouts and exported signatures change within unfrozen
ABI 4; rebuild applications and plugins with matching 0.8.30 headers/libraries.

Positional snapshot results gain producer-specific residual counts and the
first known skipped MakerNote offset/tag. Diagnostic projection takes the result
and output span directly; the old request-intent options are removed. Update
three-argument calls to collect_read_transfer_source_diagnostics accordingly.
The narrow Host Adoption Profile label remains v1. Standalone MRW and existing
Canon derived values now use bounded callbacks; Sony SR2-private and RAF F000
metadata have targeted memory/callback traversal. Unknown private layouts still
require explicit support and can leave residuals.

Concept resolution also caps companion/composite scans and preference/conflict
comparisons through max_semantic_work (default/zero1048576). Check
semantic_work_limited before accepting a preferred/conflict conclusion. The
aggregate shares one cap across all ten concept kinds, including RAW descriptor
variants. Direct orientation/GPS numeric vectors consume the same numeric
output budget as query-backed candidates.

The two transfer_concept_diagnostics_from_store overloads take final query
options and return semantic-work/numeric incompleteness flags. Their layouts
and signatures also require matching rebuilt0.8.30 headers and libraries.


Scoped Python services and Canon alternate reads in 0.8.31
--------------------------------------------------------

Python semantic services accept image scope and resource budgets through
explicit options. Existing calls without options retain their defaults. See
semantic_queries.md for the owned Python selector/options contract and examples.
The C++ ABI label remains the unfrozen ABI 4; rebuild against matching headers
and libraries when updating integrations.

Positional Canon FilterInfo and CustomFunctions2 expansion can follow checked alternate metadata
offsets without reading intervening bytes. Scratch, entry and I/O limits still
apply, and incomplete paths retain diagnostics. This does not add arbitrary
MakerNote scanning or sensor-pixel decoding.

CR3 positional snapshots now expand Canon CMT3 root-TIFF metadata instead of
unconditionally reporting a skipped MakerNote. The existing payload scratch
and extraction limits apply; memory and positional inputs share the decoder.

DNG DefaultCropOrigin and DefaultCropSize now require exactly two source values;
ActiveArea requires exactly four. Other lengths retain their numeric source
values and completeness but do not produce normalized origin/size/rectangle
fields. MaskedAreas still accepts multiple four-value rectangles. A Complete
numeric result describes the returned numbers; callers must also check has_rect
before consuming normalized geometry.

Canon positional parity and PNG/WebP ICC removal in 0.8.32
--------------------------------------------------------

ABI 4 remains unfrozen; this batch changes no public layout. Rebuild consumers
with matching headers and binaries. Positional Canon AFInfo2 discovery is
limited to the declared MakerNote span and 4,096 two-byte-aligned candidates.
The existing scratch and source I/O ceilings remain in effect. CNCV, CMP1 and
CTBO use shared metadata parsers in memory and positional snapshots. Remaining
CR3 residuals must still be checked; source-locator interpretation does not
select a sensor plane or decode RAW pixels.

`remove_existing_icc` now supports existing PNG and WebP targets in addition
to JPEG/TIFF/DNG. Omission still preserves destination ICC. Removal suppresses
source ICC and rejects conflicting ICC blocks, including manually constructed
empty blocks. PNG/WebP package preflight checks the virtual output before
publication; WebP also requires the VP8X ICC flag to be clear. Fresh emission
and payload-only/codec handoffs still reject this edit-only intent.

AFInfo2 geometry arrays now retain signed 16-bit types, and focus/selection
masks retain their full word counts. This corrects the previous scalar mask
truncation and later-field offsets. Selection and primary-point meanings depend
on known model/record context; ambiguous and surplus data remain raw. Callers
that assumed one scalar for a multiword mask must use the reported value shape,
wire type and count. These are metadata interpretation corrections within ABI 4.
