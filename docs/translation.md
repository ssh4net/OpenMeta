# Metadata Translation

`openmeta/metadata_translation.h` provides bounded explicit projection between
metadata families. Current contracts translate edited XMP creation dates into
native EXIF/IPTC date groups and exact descriptive XMP properties into native
IPTC-IIM datasets before transfer or writing.

The APIs are experimental and versioned by
`kMetadataDateTranslationContractVersion == 1` and
`kMetadataDescriptiveTranslationContractVersion == 1`.

## Workflow

Translation is a separate step. Creation, editing, transfer, and writing do not
invoke it implicitly:

1. Read, create, or edit a finalized `MetaStore`.
2. Call `translate_xmp_creation_dates(...)`,
   `translate_xmp_descriptive_metadata(...)`, or both with explicit mapping
   and conflict options.
3. Pass the returned finalized store to transfer preparation or a writer.

This separation prevents a transfer from unexpectedly replacing native camera
dates merely because decoded XMP is present. The default source mode is
`DirtyOnly`, so only caller-modified XMP values and tombstones are eligible.

## Date Mappings

| XMP source | Native destination | Precision requirements |
| --- | --- | --- |
| `xmp:CreateDate` | EXIF `DateTimeDigitized`, `OffsetTimeDigitized`, and `SubSecTimeDigitized` | Time is required. Timezone and up to nine fractional digits are preserved in companion tags. |
| `xmp:CreateDate` | IPTC `DigitalCreationDate` and `DigitalCreationTime` | Date-only or whole seconds are accepted. Fractional seconds are rejected. |
| `photoshop:DateCreated` | IPTC `DateCreated` and `TimeCreated` | Date-only or whole seconds are accepted. Fractional seconds are rejected. |
| XMP `exif:DateTimeOriginal` | EXIF `DateTimeOriginal`, `OffsetTimeOriginal`, and `SubSecTimeOriginal` | Time is required. Timezone and up to nine fractional digits are preserved in companion tags. |

Accepted values use a full Gregorian `YYYY-MM-DD` date, optionally followed by
`T` and `hh:mm:ss`, up to nine fractional digits, and `Z` or a `+/-HH:MM`
timezone. Invalid dates, malformed values, and conversions that would discard
precision fail instead of being normalized or truncated. Lexical `-00:00` is
preserved as a negative-zero offset.

Each mapping can be disabled independently. For example, callers that need to
retain fractional `xmp:CreateDate` can disable its IPTC projection while
keeping exact EXIF projection.

## Descriptive Mappings

| XMP source | Native IPTC-IIM destination | Maximum encoded bytes |
| --- | --- | --- |
| `dc:title[@xml:lang=x-default]` | `ObjectName` (2:5) | 64 |
| `dc:description[@xml:lang=x-default]` | `Caption-Abstract` (2:120) | 2000 |
| `dc:creator[n]` | repeated `By-line` (2:80) | 32 per value |
| `dc:subject[n]` | repeated `Keywords` (2:25) | 64 per value |
| `dc:rights[@xml:lang=x-default]` | `CopyrightNotice` (2:116) | 128 |
| `photoshop:Credit` | `Credit` (2:110) | 32 |
| `photoshop:Source` | `Source` (2:115) | 32 |

The default-language and indexed paths must match exactly. Creator and keyword
items retain numeric XMP index order. Duplicate singleton properties or
duplicate active indexes are ambiguous and fail rather than selecting a value.
IPTC-IIM limits are byte limits; text is never truncated.

Non-ASCII values remain UTF-8. Translation emits IPTC `CodedCharacterSet`
(1:90) with `ESC % G` when the marker is absent and every existing active IPTC
value is ASCII or will be replaced by the same transaction. An incompatible
charset marker or unrelated legacy high-bit data returns
`NativeEncodingConflict` without modifying the output.

## Conflict And Removal Policy

The date and descriptive conflict-policy enums apply the same three behaviors
to each complete native group:

| Policy | Behavior |
| --- | --- |
| `PreserveExisting` | Keep the complete native group when any member already exists. |
| `FailOnConflict` | Require the group to be absent or already exactly equivalent. This is the default. |
| `ReplaceExisting` | Replace the group, tombstone duplicates, and tombstone stale companion fields. |

A dirty deleted XMP source is propagated only with `ReplaceExisting`; the
corresponding native group is tombstoned. Duplicate eligible XMP source
properties are rejected as ambiguous rather than choosing one occurrence.

The operation is transactional. The source is immutable and the output store
is replaced only after all selected mappings parse, reconcile, and finalize.
New native entries retain the source XMP block and wire provenance, with any
referenced wire-type name copied into output-owned storage.
`max_added_entries` and `max_operations` may lower the public hard limits of 16
added entries and 1024 native operations. Calls keep no global state and are
safe when each concurrent call owns its output store.

Descriptive translation separately bounds matched source properties, added
entries, operations, and total text bytes. In `DirtyOnly` mode, one dirty member
makes the complete repeated creator/keyword group eligible, so unchanged active
members are retained while dirty tombstones can remove native values under
`ReplaceExisting`.

## C++ Example

```cpp
#include "openmeta/metadata_translation.h"

openmeta::MetadataDateTranslationOptions options;
options.conflict_policy
    = openmeta::MetadataDateTranslationConflictPolicy::ReplaceExisting;

openmeta::MetaStore translated;
const openmeta::MetadataDateTranslationResult result
    = openmeta::translate_xmp_creation_dates(edited, options, &translated);
if (result.status != openmeta::MetadataDateTranslationStatus::Ok) {
    // translated is unchanged.
}

openmeta::MetadataDescriptiveTranslationOptions descriptive_options;
descriptive_options.conflict_policy
    = openmeta::MetadataDescriptiveTranslationConflictPolicy::ReplaceExisting;
openmeta::MetaStore descriptive;
const auto descriptive_result = openmeta::translate_xmp_descriptive_metadata(
    translated, descriptive_options, &descriptive);
```

## Python

Python calls the same C++ transaction and returns a detached `Document`:

```python
translated = edited.translate_creation_dates(
    conflict_policy=openmeta.MetadataDateTranslationConflictPolicy.ReplaceExisting,
)
translated = translated.translate_descriptive_metadata(
    conflict_policy=openmeta.MetadataDescriptiveTranslationConflictPolicy.ReplaceExisting,
)
```

Invalid or lossy requests raise `ValueError` containing the C++ status,
mapping, and source entry ID. The original `Document` is not mutated.

## Scope

This milestone is intentionally limited to exact creation-date and common
descriptive mappings. It does not yet provide arbitrary EXIF/IPTC/XMP
translation, multilingual-alternative selection, timezone inference, value
repair, technical EXIF reverse projection, or automatic synchronization during
transfer.
