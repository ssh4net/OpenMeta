# Migrating to OpenMeta 0.8

OpenMeta 0.8.0 retains the **unfrozen development ABI label 4**. The public
`PreparedTransferBundle` layout changes: rebuild applications and plugins against
matching headers and libraries. Request `find_package(OpenMeta 0.8 CONFIG REQUIRED)`.
The installed `SameMinorVersion` policy rejects 0.7 package requests. ABI label 4
alone does not establish compatibility with older binaries.

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
