# FlatHost Mapping Contract

`ExportNameStyle::FlatHost` is the stable v1 naming contract for host-owned
metadata object models that prefer flat attribute names over OpenMeta's native
key shapes.

Contract constant:

```cpp
openmeta::kFlatHostExportContractVersion == 1
```

## Scope

The contract covers names emitted by:

```cpp
openmeta::visit_metadata(store, options, sink)
```

with:

```cpp
options.style = openmeta::ExportNameStyle::FlatHost;
```

Python mirrors this name contract through:

```python
doc.export_names(style=openmeta.ExportNameStyle.FlatHost)
```

The C++ traversal exposes `ExportItem::entry`, so C++ hosts can project values
from the original `Entry::value`. Python `export_names(...)` is name-only.

Typed writeback is available through `import_flat_host_metadata(...)`. It is
transactional and returns a detached finalized store; it does not mutate the
source store.

## Ordering And Duplicates

- Entries are emitted in `MetaStore::entries()` order.
- Deleted entries are skipped.
- Duplicate names are preserved and emitted separately.
- OpenMeta does not merge, reconcile, or suffix duplicate `FlatHost` names.
- If a host requires unique keys, it must apply its own deterministic collision
  policy after traversal.
- Name-based import succeeds only for a unique exported name. A host that must
  update one duplicate retains the source `EntryId` and imports by identity.

## Value Projection

`FlatHost` is a naming contract, not a value-conversion contract.

- C++ receives the original `Entry` through `ExportItem::entry`.
- `Entry::value.kind`, `elem_type`, `count`, and storage are unchanged.
- Text encoding remains the original decoded `TextEncoding`.
- Byte payloads remain byte payloads; unsafe text conversion is not performed
  by `visit_metadata(...)`.
- Hosts that need a text-only export should use a safe formatting layer on top
  of `Entry::value`.

## Namespace Behavior

`FlatHost` intentionally keeps common camera-style names short while preserving
namespace prefixes where ambiguity matters.

| Source family | FlatHost rule |
| --- | --- |
| TIFF root and preview IFD tags | Use the known tag alias without a prefix, for example `Make`, `ModifyDate`, `ImageWidth`. |
| EXIF and interoperability IFD tags | Prefix with `Exif:`, for example `Exif:ExposureTime`, `Exif:ISO`, `Exif:CreateDate`. |
| GPS IFD tags | Prefix with `GPS:`, for example `GPS:GPSLatitude`. |
| MakerNote IFDs | Emit only when `include_makernotes` is true, using `MakerNote:<ifd>:<tag-name-or-hex>`. |
| XMP simple properties | Emit selected known namespaces as `XMP:`, `TIFF:`, `Exif:`, or `DC:` plus the simple property name. |
| ICC header fields and tags | Emit `ICC:<field>` or `ICC:tag:<signature>`. |
| EXR part 0 known aliases | Map selected standard attributes to common host names, for example `owner -> Copyright`, `capDate -> DateTime`, `expTime -> ExposureTime`. |
| EXR part 0 unknown attributes | Emit `openexr:<name>`. |
| EXR non-zero parts | Emit `openexr:part:<index>:<name>`. |
| Unsupported key families | Fall back to the canonical OpenMeta name when one exists. |

Complex XMP paths are not flattened. XMP properties are emitted only when the
property path is a simple token containing letters, digits, `_`, or `-`; paths
with `/`, `[`, `]`, or other punctuation are skipped by `FlatHost`.

## Name Policy

`ExportOptions::name_policy` controls tag aliasing:

- `ExportNamePolicy::ExifToolAlias` is the default and uses OpenMeta's
  ExifTool-compatible aliases where OpenMeta has a clean-room mapping.
- `ExportNamePolicy::Spec` preserves native/spec-style tag names where
  available and uses `Tag_0x....` for unknown EXIF tags.

The policy does not change ordering, duplicate preservation, or value
projection.

## Typed Import

`FlatHost` names are lossy and are not parsed back into arbitrary keys.

- `FlatHostImportTarget::SourceEntry` verifies the original `EntryId` and its
  exported name before replacing the typed value.
- `FlatHostImportTarget::UniqueName` resolves exactly one exported name and
  rejects missing or duplicate matches.
- `FlatHostImportTarget::ExplicitKey` appends a new entry from a caller-supplied
  `MetaKeyView`. This is the path for custom XMP and metadata that did not exist
  in the source store.
- Values retain scalar/array/bytes/text kind, element type, count, and text
  encoding. Byte arrays are never converted to text implicitly.
- Import uses local state and is safe to call concurrently against an immutable
  finalized source store.

## Filtering

`FlatHost` skips:

- deleted entries
- EXIF pointer tags under the default alias policy
- embedded metadata blob tags such as EXIF-stored XMP, ICC, IPTC, Photoshop
  IRB, and MakerNote blob tags under the default alias policy
- MakerNote IFDs when `include_makernotes` is false
- complex XMP paths and unknown XMP namespaces
- selected structural EXR header attributes that are not useful as host
  metadata attributes, such as `channels`, `compression`, and data windows

## Stability

This is a stable v1 naming contract. Future OpenMeta releases may add mappings
for newly decoded metadata families, but existing v1 names should not change
without a new contract version or a documented compatibility path.
