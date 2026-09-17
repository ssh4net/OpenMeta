# Capture synchronization milestones: 0.5.4–0.5.9

Audit date: 2026-09-14. The original observations below describe C++ 0.5.4.
The 0.5.5 update closes the identified portable-output gaps without adding tags
or changing API signatures, ABI 3, or host synchronization responsibilities.

## EXIF text and version metadata in 0.5.9

The 0.5.9 text/version batch adds UserComment, both version fields and seven
EXIF 3 text tags. The new EXIF text API also supports UTF-8 owner/lens fields.
The combined capture inventory is 75 distinct ExifIFD tags across sixteen APIs;
compatible-file transfer retains 72. Version and Artist/Software companion
requirements are explicit. ABI 3, snapshot v1 layout and host synchronization
responsibilities remain unchanged. BOM-less big-endian UserComment snapshots
require a 0.5.9 reader. See the EXIF text/version translation contract for limits
and the recorded OIIO/ExifTool reader limitations.

## Structured capture in 0.5.8

OECF, SpatialFrequencyResponse, CFAPattern and DeviceSettingDescription add
four native targets in one API. The combined fixture now covers 65 targets
across fifteen APIs; compatible-file transfer retains 62. Exact rational pairs,
Unicode, empty settings and TIFF byte order survive the qualified round trips.
See [the contract](translation.md#structured-capture-data-058).

## Encoding and composite capture in 0.5.7

Two more APIs cover Gamma, compressed bits per pixel, component configuration,
and the three-field composite group. The current fixture has 61 native tags
across fourteen APIs. Direct serialization retains all 61; target transfer
filters the three encoding fields under the existing destination-image policy.
Composite exposure data retains exact fractions and unavailable summaries,
including raw big-endian values through snapshot persistence. See
[the contracts](translation.md#image-encoding-and-composite-capture-057).

## Typed editing and new fields in 0.5.6

The exact-key editor validates ordered EXIF/IPTC/XMP Add/Set/Remove operations
and the complete candidate before publication. It preserves source identity,
updates obsolete wire hints, and supports aliased output. See [editing.md](editing.md).
Three additional capture fields and six environment fields now share the
translation, portable-output and persistence qualification. The environment
contract preserves unknown-denominator sentinels before rational reduction.

The 0.5.6 combined fixture covers twelve APIs and 55 unique targets, typed
XMP edits, exact finite and unknown rationals, and serialized transfer snapshots
through JPEG, classic TIFF and BigTIFF add/replace paths. Python exposes the two
new translators; installed shared-library checks exercise authoring, typed
editing, translation, validation and portable output. The earlier audit evidence
below remains historical and describes its original 46-target scope.

## Fixes in 0.5.5

Primary ExifIFD FNumber, FocalLength and DigitalZoomRatio emit exact fractions.
Their native types/counts and valid value ranges are checked before claiming
generated properties. Sensitivity scalars receive the same pre-claim checks.
Under `CanonicalizeManaged`, valid generated scalar replacements remove legacy
ISO/indexed ISO and sensitivity companion aliases, including across the
`exif`/`exifEX` namespaces. Missing or invalid replacements retain source values.
Retained indexed `ISOSpeedRatings` keeps that recognized array name rather than
becoming unsupported `ISO[1]` when no replacement is available.
Replacement is per property; it does not infer or repair group relationships.

`PreserveAll` still retains legacy aliases, and reverse translation still rejects
duplicate eligible sources. Default native conflict behavior and per-call
transactions also remain unchanged. Combined tests cover all ten APIs, exact
boundaries, malformed values, policies and JPEG/classic TIFF/BigTIFF snapshots.
The earlier rounded packets cannot recover lost precision without the native
originals. FocalLength portable fractions express millimeters without ` mm`.

## Implemented capture targets

Sixteen explicit reverse APIs cover **75 distinct ExifIFD tags**. The count includes
camera text and excludes GPS, dates, geometry, IPTC and vendor MakerNotes.
It is an inventory count, not a percentage of all EXIF or competitor coverage.
All function names below have the prefix `translate_xmp_` and suffix
`_metadata`. Detailed source types, aliases, limits and removal rules are in
[Metadata translation](translation.md).

| API stem | Targets | ExifIFD tags |
| --- | ---: | --- |
| capture | 5 | ExposureTime 829A, FNumber 829D, ISO 8827, FocalLength 920A, ExposureBiasValue 9204 |
| capture_settings | 12 | ExposureProgram 8822, MeteringMode 9207, SensingMethod A217, CustomRendered A401, ExposureMode A402, WhiteBalance A403, SceneCaptureType A406, GainControl A407, Contrast A408, Saturation A409, Sharpness A40A, SubjectDistanceRange A40C |
| capture_rational | 4 | SubjectDistance 9206, DigitalZoomRatio A404, ExposureIndex A215, FlashEnergy A20B |
| flash | 1 | Flash 9209 |
| light_source | 1 | LightSource 9208 |
| sensitivity | 7 | ISO 8827, SensitivityType 8830, StandardOutputSensitivity 8831, RecommendedExposureIndex 8832, ISOSpeed 8833, ISOSpeedLatitudeyyy 8834, ISOSpeedLatitudezzz 8835 |
| camera_text | 6 | SpectralSensitivity 8824, CameraOwnerName A430, BodySerialNumber A431, LensMake A433, LensModel A434, LensSerialNumber A435 |
| identity | 2 | LensSpecification A432, ImageUniqueID A420 |
| apex | 5 | ShutterSpeedValue 9201, ApertureValue 9202, BrightnessValue 9203, ExposureBiasValue 9204, MaxApertureValue 9205 |
| capture_spatial | 5 | FocalPlaneXResolution A20E, FocalPlaneYResolution A20F, FocalPlaneResolutionUnit A210, SubjectArea 9214, SubjectLocation A214 |
| capture_additional | 3 | FocalLengthIn35mmFilm A405, FileSource A300, SceneType A301 |
| environment | 6 | Temperature 9400, Humidity 9401, Pressure 9402, WaterDepth 9403, Acceleration 9404, CameraElevationAngle 9405 |

| image_encoding | 3 | Gamma A500, CompressedBitsPerPixel 9102, ComponentsConfiguration 9101 |
| composite | 3 | CompositeImage A460, SourceImageNumberOfCompositeImage A461, SourceExposureTimesOfCompositeImage A462 |
| structured_capture | 4 | OECF 8828, SpatialFrequencyResponse A20C, CFAPattern A302, DeviceSettingDescription A40B |
| exif_text | 13 (10 new) | ExifVersion 9000, FlashpixVersion A000, UserComment 9286, ImageTitle A436, Photographer A437, ImageEditor A438, CameraFirmware A439, RAWDevelopingSoftware A43A, ImageEditingSoftware A43B, MetadataEditingSoftware A43C, CameraOwnerName A430, LensMake A433, LensModel A434 |

Tag IDs are hexadecimal. The 67 API mappings include two shared targets:
ISO belongs to basic capture and sensitivity; ExposureBiasValue belongs to
basic capture and APEX. The new text API also overlaps owner/lens tags. Counting each shared tag once gives 75.

## Historical 0.5.4 qualification and gaps

A combined fixture exercises all ten APIs with ordinary values and a second
set containing nonterminating fractions. Explicit `ReplaceExisting` produces
all 46 targets with exact native types, counts and values in JPEG and classic
TIFF. Reversing the call order gives the same native result, and repeating the
calls adds no entries. This qualifies those fixtures, not every combination
of accepted values, conflicts or containers.

The audit found the following portable-output gaps in 0.5.4:

| Case | Observed result | Consequence |
| --- | --- | --- |
| Native FNumber `17/6` | Portable XMP contains `2.8` | Reverse translation silently changes the value to `14/5`. |
| Native FocalLength `50/3` | Portable XMP contains `16.7 mm` | Reverse translation silently changes the value to `167/10`. |
| Native DigitalZoomRatio `1/3` | Portable XMP contains `0.333333333333333` | The exact reverse parser rejects the decimal because its reduced numerator/denominator do not fit the native type. The four-field rational call remains transactional. |
| Existing `exif:ISO` plus a generated sensitivity group | Both `exif:ISO` and `exifEX:PhotographicSensitivity` survive, including with `CanonicalizeManaged` | The sensitivity reverse API rejects the duplicate eligible aliases as `AmbiguousSource`. |
| Malformed native SHORT for FNumber, FocalLength or DigitalZoomRatio, with valid existing XMP | `CurrentBehavior` claims the property but emits no value | Valid existing XMP can disappear. `ExistingWins` retains it in these fixtures. |

ExposureTime, SubjectDistance, ExposureIndex and FlashEnergy retain their exact
fractions in the tested packets. Missing fields after the DigitalZoomRatio
failure reflect rollback of that whole API call; they are not four independent
formatting defects. The newer APEX, identity and spatial contracts also survive
the combined control round trip.

`ExistingWins` with `PreserveAll` retains the original rational values in the
combined fractional fixture, but still has the ISO alias collision. Disabling
EXIF projection and emitting only the retained source XMP gives an exact
46-target reverse round trip for both fixtures. That choice is suitable only
when the source XMP is authoritative and complete: it cannot publish later
native-only edits. No general synchronization workaround is claimed.

## Composition and host responsibilities

Translation and portable output are separate operations. Transfer does not
invoke the reverse APIs. See [XMP sync policy](xmp_sync_policy.md) for source,
destination, sidecar and generated-property precedence.

Each reverse call has its own transaction. With `All` source selection and the
default `FailOnConflict`, basic capture can create ISO before the sensitivity
call sees it. The latter then rejects the incomplete native sensitivity group,
even when ISO matches. In the audited fixture, calling sensitivity first passes;
explicit replacement also passes in either order. Hosts should assign ownership
of shared targets or choose a deliberate conflict policy. A partial group is
not equivalent to the requested complete group.

Stage a sequence of calls in a candidate store/document and publish it only
after all calls succeed. A late invalid SubjectLocation leaves the original
document and the earlier successful candidate intact in the Python probe.
Reassigning the host's current document after every call publishes partial work
across the sequence. This is distinct from the per-call transaction guarantee.

Retained source XMP, native entries and destination carriers each need an
explicit authority decision. Packet absence does not encode a deletion request;
dirty tombstones and carrier cleanup follow their separate documented policies.
The library does not infer photographic relationships such as FNumber/APEX,
ExposureTime/APEX, focal length/sensor dimensions or ISO/gain. Synchronization
of conflicting shared-object access remains the host's responsibility.

## Completed fix scope in 0.5.5

The combined fix retains the existing reverse API signatures and conflict
policies. Its acceptance checks are:

1. Emit exact accepted values for FNumber, FocalLength and DigitalZoomRatio,
   including boundary fractions and zero where the existing contract permits it.
2. Validate native scalar types and counts before claiming a generated property.
   Invalid native values must leave valid existing XMP available.
3. Reconcile eligible sensitivity aliases under `CanonicalizeManaged` only when
   a valid generated replacement exists. Test legacy scalar/indexed ISO paths,
   canonical companions, absent/invalid replacements and conflicting values.
   Keep `PreserveAll` and per-call ambiguity rules explicit.
4. Run one combined regression batch over all ten APIs, shared targets, policies,
   failure rollback and JPEG/classic TIFF/BigTIFF persistence. Qualify exact wire
   values and independent image reads together, then run the platform matrix
   once against the final patch.

The additional capture and environment batches are implemented in 0.5.6.
Encoding/composite contracts are implemented in 0.5.7 and structured capture
contracts in 0.5.8; text/comment/version contracts are implemented in 0.5.9.
Next, review the remaining standard EXIF authoring gaps as a group, then select
the next bounded writeback family. Fuzzy search remains lowest priority.

## Remaining field families

These are candidate families outside the sixteen capture contracts, not a complete
remaining-tags denominator. Read/display support does not imply reverse support.

| Family | Examples | Work needed before implementation |
| --- | --- | --- |
| Remaining EXIF authoring/profile rules | Mandatory-tag relationships and remaining standard fields | Inventory concrete gaps against the existing contracts before selecting another batch. |
| MakerNotes | Vendor/version-specific offsets and integrity fields | Continue the separate rewrite-trust work; generic opaque authoring does not establish safe relocation. |

The audit does not reopen downstream application acceptance or claim arbitrary
RDF synchronization, complete EXIF authoring, whole-corpus qualification or
competitor parity.
