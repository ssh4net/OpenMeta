# Generic Typed Metadata Authoring

`openmeta/metadata_authoring.h` provides the C++ v1 construction path for
applications that already know the exact metadata keys and types they need to
write. It complements the logical-field API in `metadata_creation.h`; it does
not replace that simpler API.

The API currently authors these writable families:

- EXIF/TIFF and DNG-style tags by IFD token plus numeric tag ID
- XMP properties by namespace URI plus property path
- IPTC-IIM datasets by record plus dataset number

Unknown/private EXIF tags and custom XMP namespace URIs do not require an
OpenMeta registry update. Inputs are borrowed for the call and copied into a
new finalized `MetaStore`.

## Transactional Construction

```cpp
#include "openmeta/metadata_authoring.h"

#include <array>

const std::array entries = {
    openmeta::MetadataAuthoringEntry {
        openmeta::make_exif_tag_key_view("ifd0", 0x010F),
        openmeta::make_value_view_text(
            "Example Camera", openmeta::TextEncoding::Ascii),
        openmeta::WireType { openmeta::WireFamily::Tiff, 2 },
        15,
    },
    openmeta::MetadataAuthoringEntry {
        openmeta::make_exif_tag_key_view("exififd", 0x829A),
        openmeta::make_value_view_urational(1, 125),
        openmeta::WireType { openmeta::WireFamily::Tiff, 5 },
        1,
    },
    openmeta::MetadataAuthoringEntry {
        openmeta::make_xmp_property_key_view(
            "urn:example:capture:1.0/", "Gain[1]"),
        openmeta::make_value_view_text(
            "1.25", openmeta::TextEncoding::Utf8),
    },
};

openmeta::MetadataAuthoringOptions options;
options.validation.context.has_dimensions = true;
options.validation.context.width = 4096;
options.validation.context.height = 2160;

openmeta::MetaStore store;
const openmeta::MetadataAuthoringResult result =
    openmeta::create_metadata_store(entries, &store, options);
```

`store` is replaced only after preflight, deep copy, finalization, and enabled
validation all succeed. On failure, `failed_entry` and `validation_issue`
identify the first actionable problem and the previous output remains intact.
Duplicate keys are preserved by default because metadata can legitimately be
multi-valued. Set `RejectExactKeys` when the host requires unique explicit
keys; known EXIF singleton duplicates are rejected by schema validation.

`MetaStore::reserve(blocks, entries, arena_bytes)` is also public for trusted
low-level construction. It changes capacity only and must be called before
finalization; active `constrain_resources()` ceilings are honored. The
transactional builder calculates and reserves its own capacity.

## Validation

`validate_entry()` and `validate_store()` in `openmeta/validate.h` are detached
from file decoding. Their first schema covers common TIFF, EXIF, GPS, and DNG
tags and checks:

- key and value shapes, scalar ranges, rational denominators, and text
- known tag IFD, TIFF type, count, and duplicate-singleton rules
- XMP namespace URI and supported property-path syntax
- entry, arena, value, key, and issue limits
- optional image dimensions, samples per pixel, CFA relationships, color-plane
  matrix counts, and related-entry consistency

Unknown/private EXIF tags remain allowed by default. Applications can change
`unknown_exif_tags` to warning or error. Validation is intentionally
extensible: absence from the initial schema does not mean that a private tag is
invalid.

Wire hints are optional. TIFF type hints are useful when one in-memory byte
payload may legally serialize as `BYTE`, `SBYTE`, or `UNDEFINED`. Array payloads
contain native in-memory elements; canonical TIFF serialization converts them
to its defined little-endian output.

## Custom XMP Scope

The builder accepts safe scalar and indexed custom properties now. Emit them
with `dump_xmp_portable()` using `include_existing_xmp = true` and
`XmpExistingNamespacePolicy::PreserveCustom`. The namespace URI is
authoritative; generated prefixes such as `omns1` are deterministic
presentation names.

Full RDF structures, arbitrary nested arrays, qualifiers, and caller-selected
prefix spelling are not part of this v1 authoring contract.

## Image Authority

Structural validity does not prove that metadata describes the encoded image.
The host remains authoritative for dimensions, channel layout, bit depth, CFA
layout, black/white levels, color transforms, exposure, and other image- or
frame-dependent facts. Supply validation context where available, and do not
reuse constant capture metadata for values that can vary per frame.

This is currently a C++ API. A future Python surface must remain a thin wrapper
over this implementation rather than duplicate construction or validation
logic.
