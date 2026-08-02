# Metadata Creation

`openmeta/metadata_creation.h` provides the first high-level Creation contract
for fresh metadata. A host supplies logical fields rather than EXIF tag IDs or
XMP namespace paths. OpenMeta validates the complete request and returns one
finalized `MetaStore` containing canonical portable-XMP entries.

The API is experimental and versioned by
`kMetadataCreationContractVersion == 1`.

## C++ Example

```cpp
#include "openmeta/metadata_creation.h"

#include <array>

const std::array fields = {
    openmeta::make_metadata_creation_text(
        openmeta::MetadataCreationFieldKind::Title, "Evening frame"),
    openmeta::make_metadata_creation_text(
        openmeta::MetadataCreationFieldKind::Creator, "Alice"),
    openmeta::make_metadata_creation_text(
        openmeta::MetadataCreationFieldKind::Keyword, "night"),
    openmeta::make_metadata_creation_u32(
        openmeta::MetadataCreationFieldKind::Orientation, 6),
    openmeta::make_metadata_creation_urational(
        openmeta::MetadataCreationFieldKind::ExposureTime, 1, 125),
};

openmeta::MetadataCreationRequest request;
request.fields = fields;

openmeta::MetaStore store;
const openmeta::MetadataCreationResult result =
    openmeta::create_metadata(request, &store);
if (result.status != openmeta::MetadataCreationStatus::Ok) {
    // result.failed_field_index identifies the rejected field when available.
}
```

The call is transactional. `store` is replaced only after every field has
passed validation and the new store has been finalized. An empty request
creates an empty finalized store.

## Current Field Mapping

| Logical field | Input type | Canonical portable-XMP property | Notes |
| --- | --- | --- | --- |
| `Title` | Text | `dc:title[x-default]` | Singleton language alternative |
| `Description` | Text | `dc:description[x-default]` | Singleton language alternative |
| `Creator` | Text | `dc:creator[n]` | Additive ordered sequence |
| `Keyword` | Text | `dc:subject[n]` | Additive unordered bag |
| `Copyright` | Text | `dc:rights[x-default]` | Singleton language alternative |
| `RightsUsageTerms` | Text | `xmpRights:UsageTerms[x-default]` | Singleton language alternative |
| `Credit` | Text | `photoshop:Credit` | Singleton |
| `Source` | Text | `photoshop:Source` | Singleton |
| `CreateDate` | Text | `xmp:CreateDate` | Caller supplies the XMP date lexical value |
| `ModifyDate` | Text | `xmp:ModifyDate` | Caller supplies the XMP date lexical value |
| `Rating` | Signed integer | `xmp:Rating` | `-1..5` |
| `Label` | Text | `xmp:Label` | Singleton |
| `CameraMake` | Text | `tiff:Make` | Singleton |
| `CameraModel` | Text | `tiff:Model` | Singleton |
| `Software` | Text | `xmp:CreatorTool` | Singleton |
| `DateTimeOriginal` | Text | `exif:DateTimeOriginal` | Caller supplies the XMP date lexical value |
| `Orientation` | Unsigned integer | `tiff:Orientation` | EXIF orientation index `1..8` |
| `PixelWidth` | Unsigned integer | `exif:ExifImageWidth` | Must be nonzero |
| `PixelHeight` | Unsigned integer | `exif:ExifImageHeight` | Must be nonzero |
| `ColorSpace` | Unsigned integer | `exif:ColorSpace` | `1..65535` |
| `ExposureTime` | Unsigned rational | `exif:ExposureTime` | Nonzero numerator and denominator |
| `FNumber` | Unsigned rational | `exif:FNumber` | Nonzero numerator and denominator |
| `IsoSensitivity` | Unsigned integer | `exif:ISO` | Must be nonzero |
| `FocalLength` | Unsigned rational | `exif:FocalLength` | Nonzero numerator and denominator |

Creators and keywords may be repeated and retain request order. Every other
logical field is a singleton; duplicate singleton fields are rejected rather
than silently selecting a winner.

## Validation And Limits

Text must be non-empty valid UTF-8 containing only XML 1.0 character data.
The current hard maxima are:

- `1024` fields
- `1 MiB` of UTF-8 text per field
- `8 MiB` of UTF-8 text per request

`MetadataCreationRequest::limits` may lower but not raise these bounds. Date
strings are not parsed or normalized in this milestone; callers must provide
valid XMP date lexical values.

Each result identifies its stable status and, when applicable, the rejected
field index. Creation uses no global state. Concurrent calls are safe when
each call has a distinct output store.

## Python

Python owns field text until the C++ request completes and uses the same C++
validation and mapping:

```python
import openmeta

K = openmeta.MetadataCreationFieldKind
document = openmeta.create_metadata([
    openmeta.metadata_creation_text(K.Title, "Evening frame"),
    openmeta.metadata_creation_text(K.Creator, "Alice"),
    openmeta.metadata_creation_u32(K.Orientation, 6),
    openmeta.metadata_creation_urational(K.ExposureTime, 1, 125),
])

packet, result = document.dump_xmp_portable(
    include_exif=False,
    include_iptc=False,
    include_existing_xmp=True,
)
```

Invalid requests raise `ValueError` with the C++ status and rejected field
index. The returned object is a normal `Document`: it can be queried, dumped,
or converted into a transfer snapshot.

Use the matching transactional Editing API to modify these fields in an
existing finalized store. See [editing.md](editing.md).

## Scope And Safety

Creation writes canonical portable-XMP entries because that representation can
flow through the existing sidecar and transfer writers without requiring an
original file layout. Direct EXIF/IPTC projection is a Translation concern and
is not implied by this API.

Image-dependent values such as dimensions, orientation, and color space must
describe the destination pixels. OpenMeta cannot infer them from an encoder
buffer. Do not copy those values from a differently sized, rotated, converted,
or color-transformed source image.

This milestone does not yet cover arbitrary custom properties, non-default
language alternatives, structured XMP records, binary values, fresh ICC
profiles, or direct EXIF/IPTC block construction.
