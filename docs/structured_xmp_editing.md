# Structured XMP creation and editing

OpenMeta 0.8.14 adds `edit_metadata_xmp()` in
`openmeta/metadata_xmp_editing.h`. It applies a bounded transaction to complete
XMP structures, array items or language alternatives. Use a finalized empty
`MetaStore` with Add operations for fresh creation. The current contract version 2 is
`kMetadataXmpEditingContractVersion`.

The API reuses the store's exact namespace URI and flattened property paths.
Each operation supplies a scope and borrowed `MetadataAuthoringEntry` values
with complete XMP keys. Leaves use ASCII or UTF-8 text; custom containers use
explicit Empty declarations. Keys and values are copied
before successful output publication. The generic typed authoring and editing
APIs remain available for individual keys.

## Standard scopes

| Namespace | Root scopes | Item/member selection |
| --- | --- | --- |
| IPTC Core `http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/` | `CreatorContactInfo`, `LocationCreated` | Whole structure |
| IPTC Extension `http://iptc.org/std/Iptc4xmpExt/2008-02-29/` | `LocationShown` | Whole array or `LocationShown[n]` |
| PLUS `http://ns.useplus.org/ldf/xmp/1.0/` | `Licensee` | Whole array or `Licensee[n]` |
| Dublin Core `http://purl.org/dc/elements/1.1/` | `title`, `description`, `rights` | Whole language array or `title[@xml:lang=en-US]`, etc. |
| XMP Rights `http://ns.adobe.com/xap/1.0/rights/` | `UsageTerms`, `Owner` | Whole language array/language member, or whole owner array/`Owner[n]` |

Roots are unqualified under the exact namespace URI. Structured leaves use
canonical writer paths, for example `CreatorContactInfo/CiEmailWork`,
`CreatorContactInfo/CiAdrCity[@xml:lang=x-default]`,
`CreatorContactInfo/CiAdrRegion/ProvinceCode[1]`,
`LocationShown[1]/City`, `LocationShown[1]/xmp:Identifier[1]`,
`LocationShown[1]/exif:GPSLatitude`, and `Licensee[1]/LicenseeName`.

The helper shares the portable writer's shape rules. This validates structural
representability, not every standard field's semantic meaning. Safe local child
names can be used where that writer supports them. Known fields must use their
required shape. For example, CiAdrCity requires an explicit language selector;
its old scalar-to-x-default compatibility promotion is not accepted by this API.
The location `Address/City` compatibility path serializes as flat `City`, so use
`LocationShown[n]/City` for an exact canonical path.

Unknown nested namespace prefixes, redundant same-namespace child prefixes,
qualified roots, compatibility aliases, noncanonical indices and unsupported
shapes fail before output mutation. Cross-namespace location children use the
registered canonical `xmp:Identifier` and `exif:GPS*` shapes. Custom trees use the explicit declarations described below.

## Operation semantics

| Operation | Behavior |
| --- | --- |
| Add | Requires an absent scope and nonempty leaf entries. |
| Replace | Requires an existing scope and nonempty replacement leaves. Matching keys retain entry identity and provenance; omitted leaves become dirty tombstones; new keys become dirty additions. |
| Remove | Requires an existing scope and an empty entries span. All selected active leaves become dirty tombstones. |

An item selector `[1]` never selects `[10]`. A language selector selects that
exact language spelling for standard scopes; custom language selection is case-insensitive. Operations observe earlier operations in request
order. Duplicate active or supplied keys, conflicting scalar/array/resource
shapes, and unsupported or truncated/unreadable selected data reject the
transaction. Sibling entries under the same namespace/root are checked for
conflicts even when only one array item is selected. A complete-record Replace
intentionally removes that record's omitted fields. Other records, namespaces
and language members remain unchanged.

```cpp
#include "openmeta/metadata_xmp_editing.h"
#include <array>

constexpr std::string_view core =
    "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
const std::array fields = {
    openmeta::MetadataAuthoringEntry {
        openmeta::make_xmp_property_key_view(
            core, "CreatorContactInfo/CiEmailWork"),
        openmeta::make_value_view_text(
            "editor@example.test", openmeta::TextEncoding::Utf8),
    },
};
const std::array operations = {
    openmeta::MetadataXmpEditingOperation {
        openmeta::MetadataXmpEditingOperationKind::Add,
        core, "CreatorContactInfo", fields,
    },
};
openmeta::MetaStore store;
store.finalize();
const auto result = openmeta::edit_metadata_xmp(store, operations, &store);
```

The base must be finalized. Base and output may alias. Any preparation or final
validation failure leaves output unchanged. The result provides a stable status,
failed group-operation index when attributable, expanded-operation count, entry
counts, and a typed-edit status for underlying validation failures.

## Bounds and serialization

Preparation allocates bounded scratch state and expands the request into one
`edit_metadata_typed()` transaction. It does not commit a new store for each
operation and is not allocation-free patch replay. Limits cover up to 1,024 group
operations, up to 4,096 expanded operations, tracked/input entries, key and
value bytes, request bytes and output arena size. `options.typed` supplies the
existing store/byte/validation ceilings. The request-byte ceiling bounds all
supplied key and payload bytes before text scanning, including inputs that a
later operation replaces. Expanded transaction bytes are checked separately. `max_scope_entries` bounds each of the tracked active entries, targeted root
groups and cumulative supplied entries, including declarations. Standard paths
have at most three segments and
canonical positive array indices at most 65,535. Lower caller limits apply too.

Serialize the edited store with `dump_xmp_portable()` and
`include_existing_xmp = true`, or pass it to `prepare_metadata_for_target()`.
Use an explicit existing-XMP conflict policy if generated EXIF/IPTC projections
can compete with edited XMP. Transfer's normal filtering and namespace policies
still apply; this API does not override them.

Stored index gaps are allowed. Indices give order within the request and are not
permanent record identifiers: serialization compacts live items into RDF list
positions without renumbering unrelated stored keys. For standard leaf-only roots, original Bag/Seq choice,
`rdf:resource` spelling and arbitrary RDF attributes are absent from the flat
store. The supported qualifier is `xml:lang`. Unrelated represented entries and
language alternatives are preserved; original XML bytes are not a round-trip
contract. Editing and XML export retain valid supplied text, while the existing
XMP decoder still trims boundary ASCII whitespace for these properties.

ABI label 4 and source snapshot v1 remain. The 0.8.26 option layouts grow
within unfrozen ABI 4. Rebuild consumers with matching headers and binaries. Hosts own synchronization of mutable objects; no internal atomics or
mutexes are introduced.

## Explicit custom trees (0.8.26)

Custom URI roots support persistent `Struct`, `Bag`, `Seq` and `LangAlt`
declarations, including empty containers, nested structures, arrays of records
and nested arrays. `make_xmp_container_entry(namespace_uri, path, kind)` creates
an Empty value with a reserved Other wire form. Text leaves remain ordinary XMP
entries. Do not use declaration wire codes for other values. Snapshot wire v1
preserves these existing entry fields exactly.

Every container needs an explicit declaration, including the root and indexed
container items. For example:

```cpp
constexpr std::string_view ns = "urn:example:session:1";
const std::array entries {
    openmeta::make_xmp_container_entry(ns, "Session", openmeta::XmpContainerKind::Struct),
    openmeta::make_xmp_container_entry(ns, "Session/Tags", openmeta::XmpContainerKind::Bag),
    openmeta::MetadataAuthoringEntry {
        openmeta::make_xmp_property_key_view(ns, "Session/Tags[3]"),
        openmeta::make_value_view_text("selected", openmeta::TextEncoding::Utf8)},
};
openmeta::MetaStore store;
const auto created = openmeta::create_metadata_store(entries, &store);
```

Array items have a homogeneous value form. Sparse positive indices are allowed;
output sorts numerically and compacts RDF positions while stored keys and IDs
remain stable. `LangAlt` items use terminal `[@xml:lang=en-US]` selectors,
compare language identity case-insensitively and emit `x-default` first when
present. Repeated selectors represent nested arrays, such as `Matrix[1][2]`.

URI identity determines names. An unqualified child uses the root schema URI.
A registered canonical prefix or `nsu_<lowercase hexadecimal UTF-8 URI>:Local`
can name a field in another namespace. Expanded QName duplicates and reserved
XML, XMLNS, RDF and OpenMeta syntax namespaces reject. Prefix spelling is not a
round-trip guarantee.

`XmpStructureLimits` applies to detached validation, authoring, typed edits and
portable output. Defaults are hard maxima: 4,096 retained declarations plus
active selected leaves, eight expanded named/selector levels, 64 decoded
namespaces, 4,096 bytes per path or URI, 16 MiB aggregate URI/path/text data and
array index 65,535. Callers may lower nonzero limits. Tombstone declarations
still identify a formerly declared root and count toward budgets. Surviving
children require live parents; a completely deleted scope is valid.

Use `MetadataValidationOptions::structures`,
`XmpPortableOptions::structures`, `XmpSidecarRequest::portable_structures` or
`PrepareTransferRequest::xmp_structures` for caller limits. Structured editing
uses `options.typed.validation.structures` and also retains its operation and
request limits. Whole-scope Add/Replace/Remove and mixed standard/custom requests
publish through one typed transaction. Semantic queries ignore declaration
markers. Custom fields stay opaque during raw-applicability filtering.

Select `include_existing_xmp=true` and `PreserveCustom` for portable output.
Selected malformed graphs and invalid options fail before span or owned-vector
writes. Existing short-span output still returns the canonical prefix with
`OutputTruncated`. Excluded custom graphs do not block standard projections.
Preparation allocates bounded scratch; this API does not extend allocation-free
prepared patch replay to changing XML structure.

Decode with `XmpDecodeOptions::preserve_custom_structures=true` to retain kinds,
empty containers and exact custom text, including boundary whitespace and CR.
Default decoding keeps historical flat entries and whitespace behavior. The
opt-in path also enforces the hard structure ceilings before copying nodes.
Decoding appends to a store and may leave partial entries on failure; discard
that scratch store when status is unsuccessful. Unsupported qualifiers,
resources, typed nodes, non-language Alt arrays and mixed structural text fail;
this is bounded XMP data support, not complete RDF fidelity or byte preservation.
The new structured API is C++; existing Python wrappers retain their documented
surface. Hosts synchronize access to shared mutable OpenMeta objects.
