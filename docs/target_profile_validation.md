# Target-profile validation

OpenMeta 0.8.13 adds opt-in, read-only validation of logical destination metadata
through `MetadataValidationOptions::target_profile`. Creation, typed editing,
`validate_store()` and canonical TIFF/EXIF serialization use the same checks.
The target-profile contract is `kMetadataTargetProfileValidationContractVersion
== 1`; the detached validation contract remains 1.

The default `MetadataTargetProfile::None` preserves detached-store behavior.
Use an enabled profile on an assembled destination candidate, after translation
and destination reconciliation. An unmerged transfer source may legitimately
lack required destination fields.

## Host facts and scope

An enabled profile requires explicit image width, height, three samples per
pixel, and a host-selected EXIF wire version of 300 or 310. The optional expected
ColorSpace accepts 1 (sRGB) or 65535 (uncalibrated). An optional expected Interop
index accepts R98, R03 or THM, independently of expected ColorSpace. A native
primary Interop directory or pointer requires explicit expected-index authority.
The validator compares that declaration; it does not derive DCF rules or ICC
agreement from it. InteroperabilityVersion remains optional.

```cpp
openmeta::MetadataValidationOptions checks;
checks.context.has_dimensions = true;
checks.context.width = 640;
checks.context.height = 480;
checks.context.has_samples_per_pixel = true;
checks.context.samples_per_pixel = 3;
checks.target_profile.profile = openmeta::MetadataTargetProfile::ExifJpeg;
checks.target_profile.exif_version = 300;
checks.target_profile.has_color_space = true;
checks.target_profile.color_space = 1;

openmeta::MetadataAuthoringOptions creation;
creation.validation = checks;
// create_metadata_store(entries, &store, creation);
openmeta::MetadataTypedEditingOptions editing;
editing.validation = checks;
// edit_metadata_typed(store, operations, &edited, editing);
openmeta::ExifTiffSerializeOptions serialization;
serialization.validation = checks;
// serialize_exif_tiff(edited, output, serialization);
```

These are bounded primary-image metadata rules based on CIPA DC-008-2023
(EXIF 3.0), Tables 17, 18 and 20 and their tag definitions. Version 310 selects
expected `0310` bytes under that rule subset; it does not claim complete EXIF
3.1 conformance. DNG, thumbnails, indexed/sub-IFDs, GPS conditional companions,
full DCF/ICC conformance, JPEG markers and pixel decoding are outside this
profile contract. The host owns image facts and container/codec selection.

## Profile rules

Every enabled profile requires native primary ExifVersion and ColorSpace.
Required values must have valid types, counts and wire hints, even when general
schema checking is disabled. Only active exact primary keys in `ifd0`,
`exififd` and `interopifd` satisfy requirements. Duplicates, tombstones and
wrong-directory substitutes cannot supply a required field.

| Profile | Required image metadata | Conditional rules |
| --- | --- | --- |
| `ExifJpeg` | ExifIFD PixelX/YDimension matching host dimensions; ComponentsConfiguration UNDEFINED `{1,2,3,0}`; IFD0 YCbCrPositioning 1 or 2 | Marker-owned IFD0 dimensions, bits, compression, samples, planar configuration and subsampling are absent from metadata. Primary strip, photometric and JPEGInterchange fields are also forbidden. |
| `ExifTiffRgbChunky` | IFD0 dimensions matching host; SHORT BitsPerSample `{8,8,8}`, Compression 1, SamplesPerPixel 3, PhotometricInterpretation 2 | PlanarConfiguration absent or 1; YCbCr fields forbidden. |
| `ExifTiffRgbPlanar` | Same RGB fields; PlanarConfiguration 2 | YCbCr fields forbidden. |
| `ExifTiffYCbCr` | Same uncompressed fields with PhotometricInterpretation 6; SHORT YCbCrSubSampling `{2,1}` or `{2,2}`; YCbCrPositioning 1 or 2 | PlanarConfiguration absent, 1 or 2. The recommended positioning/subsampling pair is not mandatory. |

Uncompressed profiles forbid compressed-only ExifIFD PixelX/YDimension,
ComponentsConfiguration, CompressedBitsPerPixel and primary Interop data.
Optional tags do not become mandatory merely because a translator uses them
as route-specific companions. The validator reports conflicts without repairing
values, upgrading EXIF versions or synthesizing tags.

For uncompressed profiles, `require_storage_tags = true` also requires positive
RowsPerStrip, StripOffsets and StripByteCounts, with paired counts equal to
`ceil(height / rows_per_strip)` times three for planar data, or times one for
chunky data. Without that option, the writer may supply storage tags later.
The validator does not prove that offsets point inside a file, that pixel bytes
exist, or that strip lengths match pixel data. Directory pointers need not be
manually authored; the canonical serializer materializes populated directories.
This API validates logical metadata rather than complete image files.

## Failure and transaction behavior

Invalid or missing host facts return `InvalidArgument` from detached validation
and `InvalidOptions` from authoring, typed editing or serialization. Missing,
forbidden, conflicting or unauthorized metadata yields structured
`TargetProfile*` issues and fails the candidate. `validate_store()` exposes the
issue list; construction and typed editing expose the first issue. Serialization
returns `InvalidMetadata`; call `validate_store()` for diagnostic detail. Existing
issue, entry and arena
budgets remain effective. `validate_entry()` checks an entry and option validity;
store-wide required companions are checked by `validate_store()`.

Construction and typed editing replace the output store only after the whole
candidate passes. A remove-and-add transaction may restore a required field
before final validation. Serialization validates before writing output bytes.
An enabled profile with `validate = false` is an invalid option combination for
construction and serialization. Profile `None` retains the existing opt-out.

The new context enlarges validation/authoring/editing/serialization option
layouts. ABI label 4 remains unfrozen by project policy. Rebuild consumers with
matching 0.8.13 headers and binaries; do not mix option layouts across versions.
Host synchronization and source snapshot v1 remain unchanged.
